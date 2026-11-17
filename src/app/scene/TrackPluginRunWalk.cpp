//
// TrackPluginRunWalk.cpp
//
#include "TrackPluginRunWalk.h"

#include "../../core/Epsilon.h"

#include <algorithm>
#include <cmath>
#include <tuple>

#include "../../world/alignment/GuidJson.h"
#include "TrackPluginPlacement.h"
#include "TrackPluginPlacementTuning.h"

namespace rs::app {

// edgeのpluginSectionsにfolderUtf8が含まれるか(区間は
// 問わない、run/chainの区間分割=どのedgeがこのentryに参加するかの粗い判定にのみ
// 使う。edge内のs範囲での実際の絞り込みはEntryActiveAtが担う)。
bool EdgeHasEntry(const rs::world::TrackEdge& edge, const std::string& folderUtf8) {
    return rs::world::EdgeHasFolder(edge, folderUtf8);
}

// edge内プラグイン区間。graph上のedge+弧長sの1点が、folderUtf8の
// 有効範囲(飛び地があれば複数)内にあるか。edgeが無い/範囲が無いなら false。
// Pier固定間隔配置・Pole径間走査の両方で、生成した各点をedge内のs範囲で最終
// フィルタするために使う(run/chain境界の粗い判定=EdgeHasEntryとは別)。
bool EntryActiveAt(const rs::world::TrackGraph& graph, const rs::world::Guid& edge, double s,
                   const std::string& folderUtf8) {
    const auto it = graph.edges.find(edge);
    if (it == graph.edges.end()) return false;
    if (it->second.spline.empty()) return false;
    const double total = static_cast<double>(it->second.spline.back().s);
    for (const std::pair<double, double>& range :
        rs::world::FolderRanges(it->second, folderUtf8, total)) {
        if (s >= range.first - rs::core::kPlacementPositionEpsM && s <= range.second + rs::core::kPlacementPositionEpsM) return true;
    }
    return false;
}

// 複線一括(trackNum>1)エントリの配置先チェーン: 中心線edge(束ねる本数は問わない)を
// 各1-edge chain にして返す。既存の run/interval 配置ロジックへ
// そのまま流せる(factoring)。単線用は呼び出し側で実レールchainを使う。
std::vector<rs::world::EdgeChain> CenterlineChainsFor(const rs::world::TrackGraph& graph,
                                                      int trackNum) {
    std::vector<rs::world::EdgeChain> chains;
    if (trackNum <= 1) return chains;
    for (const auto& kv : graph.edges) {
        const rs::world::TrackEdge& e = kv.second;
        if (rs::world::EdgeAcceptsTrackNum(e, trackNum)) {
            rs::world::EdgeChain ch;
            ch.steps.push_back(rs::world::EdgeChainStep{e.guid, 1});
            chains.push_back(std::move(ch));
        }
    }
    return chains;
}

std::vector<ChainSegment> BuildChainSegments(const rs::world::EdgeChain& chain,
                                             const rs::world::TrackGraph& graph, float& outTotalLength) {
    std::vector<ChainSegment> segs;
    segs.reserve(chain.steps.size());
    float cum = 0.0f;
    for (const rs::world::EdgeChainStep& step : chain.steps) {
        const auto it = graph.edges.find(step.edge);
        if (it == graph.edges.end() || it->second.spline.empty()) continue;
        const float length = it->second.spline.back().s;
        segs.push_back({step.edge, step.direction, length, cum});
        cum += length;
    }
    outTotalLength = cum;
    return segs;
}

AbsSLocation LocateAbsS(const std::vector<ChainSegment>& segs, size_t i, size_t j, float absS) {
    const ChainSegment* seg = &segs[j - 1];
    for (size_t k = i; k < j; ++k) {
        if (absS <= segs[k].cumStart + segs[k].length + rs::core::kChainBoundaryEpsM) {
            seg = &segs[k];
            break;
        }
    }
    const float t = std::clamp(absS - seg->cumStart, 0.0f, seg->length);
    const float localS = seg->direction == +1 ? t : (seg->length - t);
    return AbsSLocation{seg, localS};
}

// segs[i..j) の範囲内でチェーン先頭からの弧長absSに対応するedge/localSを求め、
// EvaluateAt()で軌道フレーム(位置・接線・アップ)を返す。Pier/Poleの両方の
// 配置ループから使う共通ヘルパー。
rs::world::TrackFrame FrameAtAbsS(const std::vector<ChainSegment>& segs, size_t i, size_t j,
                                  const rs::world::TrackGraph& graph, float absS) {
    const AbsSLocation loc = LocateAbsS(segs, i, j, absS);
    const auto edgeIt = graph.edges.find(loc.seg->edge);
    if (edgeIt == graph.edges.end()) return rs::world::TrackFrame{};
    return rs::world::EvaluateAtRest(edgeIt->second, static_cast<double>(loc.localS), loc.seg->direction);
}

// segs[i..j) の範囲内でabsSに対応するTrackPositionを求める(SceneEntity配置用。
// FrameAtAbsSと同じseg探索をTrackPosition向けに行う)。
rs::world::TrackPosition TrackPositionAtAbsS(const std::vector<ChainSegment>& segs, size_t i, size_t j,
                                             float absS) {
    const AbsSLocation loc = LocateAbsS(segs, i, j, absS);
    return rs::world::TrackPosition{loc.seg->edge, static_cast<double>(loc.localS), loc.seg->direction};
}

namespace {

// 1つのrunを走査するときに変わらない入力。
struct PoleWalkContext {
    const TrackPluginEntry* poleEntry = nullptr;
    const TrackPluginEntry* lineEntry = nullptr;
    const rs::world::TrackGraph* graph = nullptr;
    const std::vector<rs::world::RunAnchor>* runAnchors = nullptr;
    float maxInterval = 0.0f;
    float maxDeflection = 0.0f;
};

// segs[i..j) が今回のrun。たわみ量制限付き可変間隔で
// Poleを走査配置する(RailSim IIのCLineBuildCurve::FinishTrace()準拠、
// railsim2_ref/CLineBuildCurve.cpp:66-173)。アンカー(直前に
// 打ったPoleの位置・接線)からの距離がmaxIntervalに達するか、
// アンカー接線の直線からの横偏差がmaxDeflectionに達したら
// 次のPoleを打つ。fine刻み(kWalkStepM)で判定するため、RailSim IIの
// セグメント境界での線形逆算(dlim)は不要(十分細かく追従できる)。
PoleWalkRun WalkSingleRun(const PoleWalkContext& ctx, const std::vector<ChainSegment>& segs,
                          size_t i, size_t j, size_t runEndIndex, size_t runIndex) {
    const TrackPluginEntry& poleEntry = *ctx.poleEntry;
    const TrackPluginEntry& lineEntry = *ctx.lineEntry;
    const rs::world::TrackGraph& graph = *ctx.graph;
    const std::vector<rs::world::RunAnchor>& runAnchors = *ctx.runAnchors;

    const float runStart = segs[i].cumStart;
    const float runEnd =
        segs[runEndIndex - 1].cumStart + segs[runEndIndex - 1].length;

    PoleWalkRun run;
    run.poleEntry = &poleEntry;
    run.lineEntry = &lineEntry;
    run.runKey = rs::world::GuidToHex(segs[i].edge) + ":" + std::to_string(runIndex);

    for (size_t k = i; k < runEndIndex; ++k) {
        run.edgeSpans.push_back(
            PoleRunEdgeSpan{segs[k].edge, segs[k].direction, segs[k].cumStart,
                           segs[k].length});
    }

    // edge内プラグイン区間: このrunは複数edgeにまたがり
    // うるが、各点の実際の(edge,localS)がPole/Line両entryの有効範囲内に
    // あるものだけを配置点として残す(EntryActiveAt)。anchor/deflection
    // 走査自体(anchorAbsS/anchorFrameの更新)はabsS基準で継続し、
    // ここでの判定は「その点を配置するかどうか」のみに影響させる
    // (走査アルゴリズム本体は変えず、出力のフィルタに留める)。
    auto addPoint = [&](float absS) {
        PoleWalkPoint pt;
        pt.position = TrackPositionAtAbsS(segs, i, j, absS);
        const rs::world::TrackFrame frame = FrameAtAbsS(segs, i, j, graph, absS);
        pt.pos = frame.pos;
        pt.dir = frame.tangent;
        pt.up = frame.up;
        pt.absS = absS;
        // 実体化済みedgeはpluginSectionsからfolderが外れているため、
        // EntryActiveAtで出力だけを止め、走査は連続して通過させる。
        if (!EntryActiveAt(graph, pt.position.edge, pt.position.s,
                          poleEntry.folderUtf8) ||
            !EntryActiveAt(graph, pt.position.edge, pt.position.s,
                          lineEntry.folderUtf8))
            return;
        run.points.push_back(pt);
    };

    // 最初の柱は起点からLineInfo.Offsetだけ離れた位置。runの長さを超えるなら、
    // 走査は1歩も進まず柱は打たない。負の値の挙動は未確認なので0として扱う。
    float anchorAbsS = runStart + std::max(lineEntry.def.line.offset, 0.0f);
    rs::world::TrackFrame anchorFrame = FrameAtAbsS(segs, i, j, graph, anchorAbsS);
    if (anchorAbsS <= runEnd) addPoint(anchorAbsS);

    // RailSim IIの実装の経験則(CLineBuildCurve.cpp:100-108)をそのまま移植:
    // Y成分をyev=3.0で重み付けしてから横偏差を求め、0.3倍したものを
    // MaxDeflectionと比較する(実際の横偏差 ≒ 比較値/0.3)。
    const TrackPluginPlacementTuning& tuning = TrackPluginPlacementTuning::Embedded();
    const float walkStepM = static_cast<float>(tuning.walkStepM);
    const float yev = static_cast<float>(tuning.yev);
    const float deflectionScale = static_cast<float>(tuning.deflectionScale);
    size_t segmentIndex = i;
    for (float cursor = anchorAbsS + walkStepM; cursor <= runEnd;
        cursor += walkStepM) {
        while (segmentIndex < runEndIndex &&
               cursor > segs[segmentIndex].cumStart + segs[segmentIndex].length +
                            rs::core::kChainBoundaryEpsM) {
            const rs::world::RunAnchor* materialized =
                rs::world::FindRunAnchor(
                    runAnchors, segs[segmentIndex].edge, poleEntry.folderUtf8);
            if (materialized != nullptr && materialized->hasAnchor) {
                anchorAbsS = segs[segmentIndex].cumStart +
                    static_cast<float>(materialized->anchorS);
                anchorFrame = FrameAtAbsS(segs, i, runEndIndex, graph, anchorAbsS);
            }
            ++segmentIndex;
        }
        const rs::world::TrackFrame frame = FrameAtAbsS(segs, i, j, graph, cursor);
        const float dist = cursor - anchorAbsS;
        rs::core::Vec3 delta = frame.pos - anchorFrame.pos;
        delta.y *= yev;
        rs::core::Vec3 dirW = anchorFrame.tangent;
        dirW.y *= yev;
        dirW = dirW.Normalized();
        const float distW = delta.Length();
        const float proj = rs::core::Vec3::Dot(delta, dirW);
        const float lateralSq = distW * distW - proj * proj;
        const float deflect = deflectionScale * std::sqrt(std::max(0.0f, lateralSq));
        if (dist >= ctx.maxInterval || deflect >= ctx.maxDeflection) {
            addPoint(cursor);
            // 実体化済みedge上でもアンカーは通常どおり進める(走査本体は
            // 変えず出力だけを止める)。edgeを抜ける時点で上の
            // while節が凍結値(anchorS)へ差し替えるため、ここで
            // 特別扱いする必要はない。
            anchorAbsS = cursor;
            anchorFrame = frame;
        }
    }
    return run;
}

} // namespace

std::vector<PoleWalkRun> WalkPoleRuns(const std::vector<TrackPluginEntry>& entries,
                                      const rs::world::TrackGraph& graph,
                                      const std::vector<rs::world::RunAnchor>& runAnchors) {
    const std::vector<rs::world::EdgeChain> chains = rs::world::BuildEdgeChains(graph);
    std::vector<PoleWalkRun> results;

    // Pole(架線柱): 間隔はPole自身ではなく同一run上で有効な
    // Lineエントリ(LineInfo.MaxInterval/MaxDeflection)に従う。Line未選択の
    // run(=Poleのfolderは有効だがLineが無い区間)にはPoleを配置しない
    // (RailSim IIもLine未選択時はPole/Lineとも生成しない、CLineBuildCurve.cpp:66-67)。
    // 同一edge上でPole/Lineの組が複数(まれ)ある場合は全組み合わせぶん走査する。
    //
    // 複線一括(Pole自身のTrackNum>1)は実レールではなく中心線(束ねる本数は問わない)
    // edgeへ配置する(RailSim IIはPolePlugin::IsMultiTrack()で判定、
    // CLineBuildCurve.cpp:66,76-83。Lineの配置先もPole側のTrackNumに従う)。
    // 中心線routingはPierと同じ2パス構成。
    for (const TrackPluginEntry& poleEntry : entries) {
        if (poleEntry.type != rs::legacy_pi::PluginType::Pole) continue;
        const int trackNum = MultiTrackNum(poleEntry.def, poleEntry.type);
        const std::vector<rs::world::EdgeChain> centerlineChains =
            (trackNum > 1) ? CenterlineChainsFor(graph, trackNum)
                           : std::vector<rs::world::EdgeChain>{};
        const std::vector<rs::world::EdgeChain>& targetChains =
            (trackNum > 1) ? centerlineChains : chains;
        for (const TrackPluginEntry& lineEntry : entries) {
            if (lineEntry.type != rs::legacy_pi::PluginType::Line) continue;
            if (!lineEntry.def.line.hasLineInfo) continue;
            const float maxInterval = lineEntry.def.line.maxInterval > 0.0f
                ? lineEntry.def.line.maxInterval
                : static_cast<float>(TrackPluginPlacementTuning::Embedded().defaultMaxIntervalM);
            const float maxDeflection =
                lineEntry.def.line.maxDeflection > 0.0f
                    ? lineEntry.def.line.maxDeflection
                    : static_cast<float>(TrackPluginPlacementTuning::Embedded().defaultMaxDeflectionM);

            for (const rs::world::EdgeChain& chain : targetChains) {
                if (chain.steps.empty()) continue;
                float totalLength = 0.0f;
                const std::vector<ChainSegment> segs = BuildChainSegments(chain, graph, totalLength);
                if (segs.empty()) continue;

                auto edgeHasPair = [&](const rs::world::Guid& edgeGuid) {
                    const auto it = graph.edges.find(edgeGuid);
                    if (it == graph.edges.end()) return false;
                    // .rs2取り込みedgeも、Rs2PlacementNormalize.cppが実リンクの有無に
                    // 関わらずPoleカテゴリfolderをpluginSectionsから除去して個体化済み
                    // (実リンク0件ならfolderは空のまま個体化)なので、EdgeHasEntryが
                    // falseになりFindRunAnchorが非nullを返す(Pierと同じ理由、
                    // CollectTrackPluginPlacementsのedgeHasEntry参照)。出自を問わず
                    // 同じ判定で正しく除外できる。架線は代わりにRs2SceneApplier.cppが
                    // 解決するImportedWireSpan(Scene直下LineList{Line{Link0/Link1}})を
                    // EditorViewport.cppが別途描画する。
                    return (EdgeHasEntry(it->second, poleEntry.folderUtf8) &&
                            EdgeHasEntry(it->second, lineEntry.folderUtf8)) ||
                           rs::world::FindRunAnchor(
                               runAnchors, edgeGuid, poleEntry.folderUtf8) != nullptr;
                };

                size_t i = 0;
                size_t runIndex = 0;
                while (i < segs.size()) {
                    size_t j;
                    std::tie(i, j) = FindNextRun(segs, i, edgeHasPair);
                    if (i >= segs.size()) break;

                    size_t runEndIndex = j;
                    for (size_t k = i; k < j; ++k) {
                        const rs::world::RunAnchor* materialized =
                            rs::world::FindRunAnchor(
                                runAnchors, segs[k].edge, poleEntry.folderUtf8);
                        if (materialized != nullptr && !materialized->hasAnchor) {
                            runEndIndex = k + 1;
                            break;
                        }
                    }

                    PoleWalkContext ctx;
                    ctx.poleEntry = &poleEntry;
                    ctx.lineEntry = &lineEntry;
                    ctx.graph = &graph;
                    ctx.runAnchors = &runAnchors;
                    ctx.maxInterval = maxInterval;
                    ctx.maxDeflection = maxDeflection;
                    PoleWalkRun run = WalkSingleRun(ctx, segs, i, j, runEndIndex, runIndex);
                    if (run.points.size() >= 1) results.push_back(std::move(run));
                    ++runIndex;
                    i = runEndIndex == j ? j : runEndIndex;
                }
            }
        }
    }
    return results;
}

} // namespace rs::app

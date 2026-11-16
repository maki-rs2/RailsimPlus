//
// TrackPluginPlacement.cpp
//
#include "TrackPluginPlacement.h"

#include "../../plugins/schema/PluginTypeTraits.h"
#include "TrackPluginPlacementTuning.h"

#include <algorithm>
#include <cmath>
#include <tuple>
#include <utility>

#include "../../world/alignment/GuidJson.h"
#include "../../world/alignment/TrackChain.h"
#include "../../world/scene/ScenePlacement.h"
#include "../plugin_session/RailTieHeights.h"

namespace rs::app {

// エントリの複線一括本数(RailSim II の TrackNum)。格納先は種別ごとに異なる(Pier=def.pier、
// Girder=def直下、Pole=def.pole)。
// 1=単線用(実レールに配置)、>1=複線一括(trackCount一致の中心線に配置)。
// ヘッダで公開し、app層(手動追加のtrackNum判定、ResolveCenterlineEdgeForと組み合わせ)
// からも同じ規約を再利用できるようにする。
int MultiTrackNum(const rs::legacy_pi::PluginDefinition& def, rs::legacy_pi::PluginType type) {
    switch (rs::legacy_pi::GetPluginTypeTraits(type).multiTrackSource) {
        case rs::legacy_pi::MultiTrackSource::Pier:   return def.pier.trackNum;
        case rs::legacy_pi::MultiTrackSource::Profile: return def.trackNum;
        case rs::legacy_pi::MultiTrackSource::Pole:   return def.pole.trackNum;
        default:                                return 1;
    }
}

float MultiTrackInterval(const rs::legacy_pi::PluginDefinition& def, rs::legacy_pi::PluginType type) {
    switch (rs::legacy_pi::GetPluginTypeTraits(type).multiTrackSource) {
        case rs::legacy_pi::MultiTrackSource::Pier:   return def.pier.trackInterval;
        case rs::legacy_pi::MultiTrackSource::Profile: return def.trackInterval;
        case rs::legacy_pi::MultiTrackSource::Pole:   return def.pole.trackInterval;
        default:                                return 0.0f;
    }
}

MultiTrackMismatch ConfirmMultiTrack(const rs::legacy_pi::PluginDefinition& def,
                                     rs::legacy_pi::PluginType type, int trackNum,
                                     float trackInterval) {
    if (trackNum != MultiTrackNum(def, type)) return MultiTrackMismatch::TrackNum;
    if (trackNum > 1 && trackInterval != MultiTrackInterval(def, type)) {
        return MultiTrackMismatch::TrackInterval;
    }
    return MultiTrackMismatch::None;
}

bool IsDiscreteRailPlacementType(rs::legacy_pi::PluginType type) {
    return rs::legacy_pi::GetPluginTypeTraits(type).discreteRailPlacement;
}

rs::world::Guid ResolveCenterlineEdgeFor(const rs::world::TrackGraph& graph,
                                         const rs::world::Guid& pickedEdge, int trackNum) {
    if (trackNum <= 1) return rs::world::NilGuid();
    const auto pickedIt = graph.edges.find(pickedEdge);
    if (pickedIt != graph.edges.end() && pickedIt->second.centerlineOnly &&
        pickedIt->second.trackCount == trackNum) {
        return pickedEdge;
    }
    for (const auto& kv : graph.edges) {
        const rs::world::TrackEdge& e = kv.second;
        if (!e.centerlineOnly || e.trackCount != trackNum) continue;
        if (std::find(e.trackEdges.begin(), e.trackEdges.end(), pickedEdge) !=
            e.trackEdges.end()) {
            return kv.first;
        }
    }
    return rs::world::NilGuid();
}

std::vector<float> WireLateralOffsetsFor(const TrackPluginEntry& poleEntry) {
    const int trackNum = MultiTrackNum(poleEntry.def, poleEntry.type);
    const float trackInterval = poleEntry.def.pole.trackInterval;
    if (trackNum <= 1 || trackInterval <= 0.0f) return {0.0f};
    std::vector<float> offsets;
    offsets.reserve(static_cast<size_t>(trackNum));
    const float center = static_cast<float>(trackNum - 1) / 2.0f;
    for (int i = 0; i < trackNum; ++i) {
        offsets.push_back((static_cast<float>(i) - center) * trackInterval);
    }
    return offsets;
}

namespace {

// 1つのPierエントリについて、各チェーンのrunへ間隔配置し、配置件数をlogへ書く。
void CollectPierEntryPlacements(const TrackPluginEntry& entry, const rs::world::TrackGraph& graph,
                                const std::vector<rs::world::EdgeChain>& chains,
                                const std::vector<rs::world::RunAnchor>& runAnchors,
                                std::vector<TrackPluginPlacementItem>& result,
                                std::vector<std::string>& log,
                                std::vector<PierCarryRecord>* pierCarryRecords) {
    const float spacingM =
        entry.def.pier.interval > 0.0f ? entry.def.pier.interval
                                        : static_cast<float>(TrackPluginPlacementTuning::Embedded().defaultPierIntervalM);
    // PierInfo.Offset: 位相を打ち直す点(run起点・アンカーを渡せないedgeの切れ目)から最初の
    // 橋脚までの距離。負の値の挙動は未確認なので0として扱う。
    const float startOffsetM = std::max(entry.def.pier.offset, 0.0f);

    // 複線一括(trackNum>1)は実レールではなく trackCount 一致の中心線edgeへ
    // 配置する。単線(trackNum<=1)は実レールchain。中心線chainは
    // 1-edge chain として同じ run/interval 配置ループへ流す(factoring)。
    const int trackNum = MultiTrackNum(entry.def, entry.type);
    const std::vector<rs::world::EdgeChain> centerlineChains =
        (trackNum > 1) ? CenterlineChainsFor(graph, trackNum)
                       : std::vector<rs::world::EdgeChain>{};
    const std::vector<rs::world::EdgeChain>& targetChains =
        (trackNum > 1) ? centerlineChains : chains;

    int placedForEntry = 0;
    for (const rs::world::EdgeChain& chain : targetChains) {
        if (chain.steps.empty()) continue;
        float totalLength = 0.0f;
        const std::vector<ChainSegment> segs = BuildChainSegments(chain, graph, totalLength);
        if (segs.empty()) continue;

        // このentryがedge独立で有効化されているedgeだけの連続区間
        // (run)へ分割し、run単位でs=0から間隔を敷き直す(runをまたいで位相が
        // ずれないよう、有効/無効の境界で区切る)。
        const auto edgeHasEntry = [&](const rs::world::Guid& edgeGuid) {
            const auto it = graph.edges.find(edgeGuid);
            if (it == graph.edges.end()) return false;
            // .rs2取り込みedgeも、Rs2PlacementNormalize.cppが実リンクの有無に
            // 関わらずPier/Poleカテゴリfolderをpluginsectionsから除去して個体化
            // 済み(実リンク0件ならfolderは空のまま個体化)なので、EdgeHasEntryが
            // falseになりFindRunAnchorが非nullを返す。出自(取り込み/敷設エディタ)を
            // 問わず同じ判定で正しく自動生成対象から除外できる。
            return EdgeHasEntry(it->second, entry.folderUtf8) ||
                   rs::world::FindRunAnchor(runAnchors, edgeGuid, entry.folderUtf8) != nullptr;
        };
        size_t i = 0;
        size_t runIndex = 0;
        while (i < segs.size()) {
            size_t j;
            std::tie(i, j) = FindNextRun(segs, i, edgeHasEntry);
            if (i >= segs.size()) break;
            // segs[i..j) が今回のrun。
            const float runStart = segs[i].cumStart;
            const std::string chainKey =
                rs::world::GuidToHex(segs[i].edge) + ":" + std::to_string(runIndex);

            // 位相原点はrun起点(runStart)固定ではなく可変にする: 実体化済み
            // edgeを抜けるところで、そのedgeが凍結した位相アンカー(anchorS)へ
            // 打ち直す。実体化済みedge上の点はEntryActiveAtで出力だけが落ちるので、
            // 走査自体はrunを通過し続ける。
            int index = 0;
            float phaseOriginAbsS = runStart + startOffsetM;
            bool hasLastPlacement = false;
            float lastPlacementAbsS = 0.0f;
            for (size_t segmentIndex = i; segmentIndex < j; ++segmentIndex) {
                const ChainSegment& segment = segs[segmentIndex];
                const float segmentEnd = segment.cumStart + segment.length;
                for (float absS = phaseOriginAbsS + static_cast<float>(index) * spacingM;
                    absS <= segmentEnd; absS += spacingM, ++index) {
                    rs::world::TrackPlacement placement;
                    placement.position = TrackPositionAtAbsS(segs, i, j, absS);
                    // edge内プラグイン区間: run自体はedge単位の粗い
                    // 有効/無効(EdgeHasEntry)で区切るが、実際の各点はさらにその
                    // (edge,localS)がこのentryのsStart/sEnd範囲内にあるものだけを
                    // 配置する(間隔の位相はrun起点から変えない、範囲外は単に間引く)。
                    if (!EntryActiveAt(graph, placement.position.edge, placement.position.s,
                                      entry.folderUtf8))
                        continue;
                    // Pierも現実の橋脚はカントで傾かないため、配置位置はカント連動
                    // させるがモデル自体はワールドup基準で直立させる(Poleと同じ扱い)。
                    placement.keepUpright = true;

                    TrackPluginPlacementItem item;
                    item.folderUtf8 = entry.folderUtf8;
                    item.placement = placement;
                    result.push_back(item);
                    ++placedForEntry;
                    hasLastPlacement = true;
                    lastPlacementAbsS = absS;
                }

                if (pierCarryRecords != nullptr) {
                    PierCarryRecord record;
                    record.edge = segment.edge;
                    record.folderUtf8 = entry.folderUtf8;
                    record.hasAnchor = hasLastPlacement;
                    if (hasLastPlacement) {
                        record.anchorS = static_cast<double>(
                            lastPlacementAbsS - segment.cumStart);
                    }
                    pierCarryRecords->push_back(std::move(record));
                }

                const rs::world::RunAnchor* materialized =
                    rs::world::FindRunAnchor(runAnchors, segment.edge, entry.folderUtf8);
                if (materialized == nullptr) continue;
                if (materialized->hasAnchor) {
                    // 凍結アンカーは負値を取りうる(edgeが間隔より短い場合)。
                    // クランプせずそのまま位相原点にする。
                    phaseOriginAbsS =
                        segment.cumStart + static_cast<float>(materialized->anchorS);
                    index = 1;
                } else {
                    // 渡すアンカーが無いのでrunをここで切り、次edgeの始点から
                    // 位相を打ち直す。
                    phaseOriginAbsS = segmentEnd + startOffsetM;
                    index = 0;
                }
            }
            ++runIndex;
            i = j;
        }
    }
    if (placedForEntry > 0) {
        log.push_back("[TrackPlugin] " + entry.displayName + ": " +
                      std::to_string(placedForEntry) + "件配置");
    }
}

// Pole(架線柱): WalkPoleRunsで走査した配置点をGPUインスタンス化する。
void CollectPolePlacements(const std::vector<TrackPluginEntry>& entries,
                           const rs::world::TrackGraph& graph,
                           const std::vector<rs::world::RunAnchor>& runAnchors,
                           std::vector<TrackPluginPlacementItem>& result,
                           std::vector<std::string>& log) {
    // Poleモデルの原点は軌道面ではなく架線支持点(RailSim IIのCLineBuildCurve.cpp:135-136:
    // `tpos+m_LinePlugin->m_TrolleyAlt*tup+m_LinePlugin->m_Height*V3UP`)。
    // TrolleyAlt(軌道面-トロリ線間)+Height(トロリ線-支持点間)をup方向へ持ち上げる。
    // WalkPoleRunsが返すpoint.posはspline(地面)基準(PoleWalkPoint定義コメント参照)
    // なので、レール天端との相対を合わせるにはそのedgeのRail+Tie積み上げ量
    // (railTopLift相当)も足す必要がある。edge.railTopLiftはメッシュ
    // 再構築時にのみ書き込まれるキャッシュで、この関数の呼び出し順序に対して
    // 更新される前提が無いため、ActiveFolders+ComputeRailTieHeightsで都度算出する。
    for (const PoleWalkRun& run : WalkPoleRuns(entries, graph, runAnchors)) {
        const float trolleyHeightM =
            run.lineEntry->def.line.trolleyAlt + run.lineEntry->def.line.height;
        for (size_t idx = 0; idx < run.points.size(); ++idx) {
            const rs::world::Guid pointEdge = run.points[idx].position.edge;
            const auto edgeIt = graph.edges.find(pointEdge);
            float railTopLift = 0.0f;
            if (edgeIt != graph.edges.end()) {
                const RailTieHeights h =
                    ComputeRailTieHeights(rs::world::ActiveFolders(edgeIt->second), entries);
                railTopLift = h.maxRailHeight + h.maxTieHeight;
            }

            rs::world::TrackPlacement placement;
            placement.position = run.points[idx].position;
            placement.verticalOffsetM = railTopLift + trolleyHeightM;
            // Poleの配置位置(左右/上下)はカント連動させるがモデル自体はカントで
            // 傾けない(現実の架線柱は直立。RailSim IIのCLineBuildCurveも同じ)。
            placement.keepUpright = true;

            TrackPluginPlacementItem item;
            item.folderUtf8 = run.poleEntry->folderUtf8;
            item.placement = placement;
            result.push_back(item);
        }
        if (!run.points.empty()) {
            log.push_back("[TrackPlugin] " + run.poleEntry->displayName + " (Line: " +
                          run.lineEntry->displayName + "): " +
                          std::to_string(run.points.size()) + "件配置");
        }
    }
}

} // namespace

std::vector<TrackPluginPlacementItem> CollectTrackPluginPlacements(
    const std::vector<TrackPluginEntry>& entries, const rs::world::TrackGraph& graph,
    std::vector<std::string>& log, const std::vector<rs::world::RunAnchor>& runAnchors,
    std::vector<PierCarryRecord>* pierCarryRecords) {
    const std::vector<rs::world::EdgeChain> chains = rs::world::BuildEdgeChains(graph);

    std::vector<TrackPluginPlacementItem> result;

    for (const TrackPluginEntry& entry : entries) {
        if (!IsDiscreteRailPlacementType(entry.type)) continue;
        CollectPierEntryPlacements(entry, graph, chains, runAnchors, result, log,
                                   pierCarryRecords);
    }

    CollectPolePlacements(entries, graph, runAnchors, result, log);

    return result;
}

} // namespace rs::app

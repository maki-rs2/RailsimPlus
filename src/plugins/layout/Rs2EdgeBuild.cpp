#include "Rs2EdgeBuild.h"

#include "../../core/Epsilon.h"
#include "Rs2CurveFit.h"
#include "Rs2Import.h"
#include "../../world/alignment/TrackGraphIntegrity.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../../world/alignment/Alignment.h"
#include "../../world/alignment/CantControlPoints.h"
#include "../../world/alignment/EdgeShapeBuild.h"
#include "../../world/alignment/TrackQuery.h"
#include "../../world/alignment/TrackTraversal.h"
#include "../../world/scene/ScenePlacement.h"

namespace rs::legacy_layout {

using rs::core::Vec3;
using rs::world::ImportedPluginLink;
using rs::world::TrackEdge;
using rs::world::TrackGraph;
using rs::world::TrackNodeSample;


namespace {

// 直線判定: 隣接Splitterの接線がほぼ同一なら「直線=端点2個のみ」。
constexpr float kStraightDirDot = 0.99999f;
constexpr float kResampleStepM = 1.0f;
// PierLink/PoleLinkの個体がedge上に載っているとみなす、接線方向の残差の上限。
constexpr double kLinkAlongToleranceM = 0.05;

// ベイク済みの up と接線からカント(tan比)を復元する。right は進行方向に対する右。
float CantOfSample(const TrackNodeSample& smp, const Vec3& right) {
    const Vec3 upGeom = Vec3::Cross(smp.tangent, right).Normalized();
    const float denominator = Vec3::Dot(smp.up, upGeom);
    return std::fabs(denominator) > rs::core::kDirectionParallelEps ? Vec3::Dot(smp.up, right) / denominator : 0.0f;
}

// 駅の番線の端が、つなぐ接続子から離れているとき、その端を論理継ぎ目として取り込む。
// 形の基準は端の実際の位置(Splitter列の端)に新しいノードを置いて持たせ、接続子は
// linkNodes に入れる。基準ノードは単独で、edge は接続子の links だけでつながる。
// 離れていなければ(通常のレール・破損のずれを含む)何もしない。
void SplitSeamEnds(const Rs2RailWay& way, const ImportedPluginIndex& famIndex, TrackGraph& graph,
                   TrackEdge& edge) {
    if (edge.spline.size() < 2 || famIndex.platformEnds.empty()) return;
    for (int end = 0; end < 2; ++end) {
        if (famIndex.platformEnds.count({way.address, end}) == 0) continue;
        rs::world::Guid& baseNode = end == 0 ? edge.nodeA : edge.nodeB;
        const auto connectorIt = graph.nodes.find(baseNode);
        if (connectorIt == graph.nodes.end()) continue;
        const TrackNodeSample& sample = end == 0 ? edge.spline.front() : edge.spline.back();
        if ((connectorIt->second.pos - sample.pos).Length() <= rs::world::kLinkEndpointToleranceM) continue;

        rs::world::TrackNode base;
        base.guid = rs::world::GenerateGuid();
        base.pos = sample.pos;
        base.tangent = sample.tangent;
        base.up = sample.up;
        Vec3 right = Vec3::Cross(Vec3(0.0f, 1.0f, 0.0f), sample.tangent).Normalized();
        if (right.Length() < rs::core::kDegenerateVectorEps) right = Vec3(1.0f, 0.0f, 0.0f);
        base.cant = CantOfSample(sample, right);
        edge.linkNodes[static_cast<size_t>(end)] = baseNode;
        baseNode = base.guid;
        graph.nodes[base.guid] = base;
    }
}

} // namespace

std::vector<TrackNodeSample> ResampleSplitterList(const std::vector<Rs2Splitter>& splitters) {
    std::vector<TrackNodeSample> out;
    if (splitters.size() < 2) return out;

    double s = 0.0;  // 累積弧長(生成済みサンプルの実距離の総和で近似)
    Vec3 prevPos = splitters[0].pos;

    auto pushSample = [&](const Vec3& pos, const Vec3& dir, const Vec3& up) {
        if (!out.empty()) {
            const double d = (pos - prevPos).Length();
            s += d;
        }
        TrackNodeSample smp;
        smp.pos = pos;
        smp.tangent = dir.Normalized();
        smp.up = up.Normalized();
        smp.s = static_cast<float>(s);
        smp.kind = rs::world::AlignmentSegment::Kind::Line;  // 判別不能のため固定
        out.push_back(smp);
        prevPos = pos;
    };

    pushSample(splitters[0].pos, splitters[0].dir, splitters[0].up);

    for (size_t i = 0; i + 1 < splitters.size(); ++i) {
        const Rs2Splitter& p0 = splitters[i];
        const Rs2Splitter& p1 = splitters[i + 1];
        const Vec3 delta = p1.pos - p0.pos;
        const float chord = delta.Length();
        if (chord < rs::core::kTrackPositionEpsM) continue;  // 重複点はスキップ

        const bool straight =
            Vec3::Dot(p0.dir.Normalized(), p1.dir.Normalized()) > kStraightDirDot;
        const int steps =
            straight ? 1 : std::max(1, static_cast<int>(std::ceil(chord / kResampleStepM)));

        for (int k = 1; k <= steps; ++k) {
            const float t = static_cast<float>(k) / static_cast<float>(steps);
            if (k == steps) {
                // 区間終端は元の Splitter 値をそのまま使う(丸め誤差の蓄積回避)。
                pushSample(p1.pos, p1.dir, p1.up);
                continue;
            }
            float h00, h10, h01, h11;
            rs::world::HermiteBasis(t, h00, h10, h01, h11);
            const Vec3 m0 = p0.dir * chord;
            const Vec3 m1 = p1.dir * chord;
            const Vec3 pos = p0.pos * h00 + m0 * h10 + p1.pos * h01 + m1 * h11;

            float d00, d10, d01, d11;
            rs::world::HermiteBasisDeriv(t, d00, d10, d01, d11);
            const Vec3 tangent = p0.pos * d00 + m0 * d10 + p1.pos * d01 + m1 * d11;

            // up は正規化線形補間(球面線形の簡略版として許容)。
            const Vec3 up = p0.up * (1.0f - t) + p1.up * t;
            pushSample(pos, tangent, up);
        }
    }
    return out;
}

namespace {

// PierLink/PoleLinkが参照するScene直下Pier/PoleインスタンスをAddressで引けるように
// 索引化する(kind="Pier"/"Pole")。
std::unordered_map<Rs2Address, const Rs2PlacedInstance*> IndexPlacementsByKind(
    const std::vector<Rs2PlacedInstance>& placements, const char* kind) {
    std::unordered_map<Rs2Address, const Rs2PlacedInstance*> out;
    for (const Rs2PlacedInstance& p : placements) {
        if (p.kind != kind || Rs2AddressIsNull(p.address)) continue;
        out[p.address] = &p;
    }
    return out;
}

// PierLink/PoleLinkの参照先Addressごとに「代表edge(Rs2RailWay::address)」を1つ選ぶ
// (同一Pole/Pierが複数edgeから重複参照されるケースの解消。
// 実データでは、複線区間で中心線と実レール複数本が同一s=0地点の1本を重複参照する)。
// 複線一括仕様は中心線(centerlineOnly)基準で配置される
// (TrackPluginPlacement.cppと同じ規約)ため中心線を優先し、無ければ最初に出現したedgeを
// 採用する(重複はs値が一致する場合しか確認されていないため、この選択は
// 現在位置には影響しない。片方のedgeだけを編集した後に選択の違いが出る)。
template <typename LinkT>
std::unordered_map<Rs2Address, Rs2Address> ChoosePrimaryWay(
    const std::vector<Rs2RailWay>& ways, std::vector<LinkT> Rs2RailWay::*links) {
    std::unordered_map<Rs2Address, bool> wayIsDummy;
    for (const Rs2RailWay& w : ways) wayIsDummy[w.address] = w.centerlineOnly;

    std::unordered_map<Rs2Address, Rs2Address> primary;
    for (const Rs2RailWay& w : ways) {
        for (const auto& link : w.*links) {
            const auto it = primary.find(link.address);
            if (it == primary.end()) {
                primary[link.address] = w.address;
            } else if (w.centerlineOnly && !wayIsDummy[it->second]) {
                primary[link.address] = w.address;
            }
        }
    }
    return primary;
}

// RailWay埋め込みPierList/PoleList.PierLink/PoleLinkから、edge上の厳密配置
// (TrackEdge::importedPiers/importedPoles)を構築する。再構築後のedge.spline
// をEvaluateAtでsサンプルし、実際の
// Pier/PoleのJointPos/Posとの差分をlateral/verticalOffsetMとして1度だけ算出する
// (以後はcurve編集後の新しいsplineに対して同じsを再評価すれば追従する、
// TrackGraph.h::ImportedPluginLink参照)。
// 個体(inst)自身の PierPlugin/PolePlugin で描画するため(RailSim II の CPier::Read/CPole::Read。
// RailWay側の値は生成時の既定にすぎない)、link1件ごとに inst.pluginName から
// フォルダを解決する。pluginName が空、または解決できない場合はメッシュ無しとする
// (folderUtf8.empty()は既存のApplyOverlayToItems側で描画対象外として扱われる)。
template <typename LinkT>
void AppendImportedPluginLinks(std::vector<ImportedPluginLink>& out, const TrackEdge& edge,
                               const std::vector<LinkT>& links, const std::string& installRootUtf8,
                               const char* kindDir,
                               const std::unordered_map<Rs2Address, const Rs2PlacedInstance*>&
                                   placedByAddress,
                               const std::unordered_map<Rs2Address, Rs2Address>& primaryWay,
                               const Rs2Address& wayAddress, std::vector<std::string>& warnings) {
    for (const LinkT& link : links) {
        const auto primaryIt = primaryWay.find(link.address);
        if (primaryIt != primaryWay.end() && primaryIt->second != wayAddress)
            continue;  // 他edgeが代表として選ばれている(重複紐付け回避)
        const auto instIt = placedByAddress.find(link.address);
        if (instIt == placedByAddress.end()) {
            warnings.push_back("RailWay " + wayAddress + ": PierLink/PoleLink先Address " +
                                link.address + " が未解決");
            continue;
        }
        const Rs2PlacedInstance& inst = *instIt->second;
        std::string folderUtf8;
        if (!inst.pluginName.empty()) {
            folderUtf8 = ResolveTrackPluginFolder(installRootUtf8, kindDir, inst.pluginName);
            if (folderUtf8.empty()) {
                warnings.push_back("RailWay " + wayAddress + ": Address " + link.address +
                                    " のプラグイン " + inst.pluginName + " が未解決");
            }
        }
        // RailSim II の描画位置は個体の Pos だけで決まり、リンクの弧長は個体を動かしても
        // 更新されない(リンクは所属を表すだけ)。弧長は Pos を射影し直して決め、edge から
        // はみ出す個体は軌道に紐付けず、絶対座標の個体として扱う。
        const std::optional<double> projectedS = rs::world::ProjectOntoEdgeFrom(
            edge, static_cast<double>(link.s), inst.pos, kLinkAlongToleranceM);
        if (!projectedS.has_value()) continue;
        const rs::world::TrackFrame frame = rs::world::EvaluateAt(edge, *projectedS, +1);
        const Vec3 right = Vec3::Cross(frame.up, frame.tangent).Normalized();
        const Vec3 delta = inst.pos - frame.pos;

        ImportedPluginLink ifl;
        ifl.folderUtf8 = std::move(folderUtf8);
        ifl.s = *projectedS;
        ifl.lateralOffsetM = Vec3::Dot(delta, right);
        ifl.verticalOffsetM = Vec3::Dot(delta, frame.up);
        // 個体の向き(水平成分)と接線の方位差を、軌道へ載せたあとも保つ。向きが接線と
        // 斜めの柱や橋脚も、保存どおりの向きで描くため。範囲は(-180, 180]。
        float yawDeg = std::fmod(rs::world::HeadingDeg(inst.dir) - rs::world::HeadingDeg(frame.tangent), 360.0f);
        if (yawDeg > 180.0f) yawDeg -= 360.0f;
        if (yawDeg <= -180.0f) yawDeg += 360.0f;
        ifl.yawOffsetDeg = yawDeg;
        ifl.address = link.address;
        out.push_back(std::move(ifl));
    }
}

// RailWay の6種プラグイン参照(kindDir, 参照名)を1つずつ試して pluginSections へ
// 単一区間として積む。
void AppendTrackPlugins(TrackEdge& edge, const Rs2RailWay& way, const std::string& installRootUtf8) {
    const std::pair<const char*, const std::string*> refs[6] = {
        {"Rail", &way.railPlugin},     {"Tie", &way.tiePlugin},
        {"Girder", &way.girderPlugin}, {"Pier", &way.pierPlugin},
        {"Line", &way.linePlugin},     {"Pole", &way.polePlugin},
    };
    std::vector<std::string> folders = rs::world::ActiveFolders(edge);
    for (const auto& [kindDir, name] : refs) {
        std::string folder = ResolveTrackPluginFolder(installRootUtf8, kindDir, *name);
        if (folder.empty()) continue;
        if (std::strcmp(kindDir, "Pier") == 0) {
            edge.importedPierFolderUtf8 = folder;
        } else if (std::strcmp(kindDir, "Pole") == 0) {
            edge.importedPoleFolderUtf8 = folder;
        }
        folders.push_back(std::move(folder));
    }
    rs::world::SetEdgeFolders(edge, std::move(folders));
}

} // namespace

ImportedPluginIndex BuildImportedPluginIndex(const Rs2Scene& scene) {
    ImportedPluginIndex index;
    index.pierByAddress = IndexPlacementsByKind(scene.placements, "Pier");
    index.poleByAddress = IndexPlacementsByKind(scene.placements, "Pole");
    index.pierPrimaryWay = ChoosePrimaryWay<Rs2PierLink>(scene.railWays, &Rs2RailWay::pierLinks);
    index.polePrimaryWay = ChoosePrimaryWay<Rs2PoleLink>(scene.railWays, &Rs2RailWay::poleLinks);
    for (const Rs2PlacedInstance& placement : scene.placements) {
        if (placement.kind != "Station") continue;
        for (const Rs2PlatformInst& platform : placement.platforms) {
            if (platform.railList.empty()) continue;
            index.platformEnds.insert({platform.railList.front(), 0});
            index.platformEnds.insert({platform.railList.back(), 1});
        }
    }
    return index;
}

TrackEdge BuildEdge(const Rs2RailWay& way, const AddressMap& addrMap, TrackGraph& graphNodes,
                    const std::string& installRootUtf8, const ImportedPluginIndex& famIndex,
                    std::vector<std::string>& warnings) {
    TrackEdge edge;
    edge.guid = addrMap.ResolveWay(way.address);
    edge.nodeA = addrMap.ResolveConnector(way.link0.connector);
    edge.nodeB = addrMap.ResolveConnector(way.link1.connector);
    if (rs::world::GuidIsNil(edge.nodeA) || rs::world::GuidIsNil(edge.nodeB)) {
        warnings.push_back("RailWay " + way.address + ": nodeA/nodeBが未解決のため取り込みスキップ");
        return edge;  // 呼び出し側が guid未解決(NilGuid)で判別しスキップする
    }

    edge.spline = ResampleSplitterList(way.splitList);
    if (edge.spline.size() < 2) {
        warnings.push_back("RailWay " + way.address + ": spline生成できず(SplitList不足)");
    }

    SplitSeamEnds(way, famIndex, graphNodes, edge);

    const auto ita = graphNodes.nodes.find(edge.nodeA);
    const auto itb = graphNodes.nodes.find(edge.nodeB);
    edge.cantA = (ita != graphNodes.nodes.end()) ? ita->second.cant : 0.0f;
    edge.cantB = (itb != graphNodes.nodes.end()) ? itb->second.cant : 0.0f;

    // RailSim II がKnotごとにベイクしたup/dirからカント(tan比)を復元する。
    if (edge.spline.size() >= 2) {
        const Vec3 worldUp(0.0f, 1.0f, 0.0f);
        Vec3 previousRight(1.0f, 0.0f, 0.0f);
        for (TrackNodeSample& smp : edge.spline) {
            Vec3 right = Vec3::Cross(worldUp, smp.tangent).Normalized();
            if (right.Length() < rs::core::kDegenerateVectorEps) right = previousRight;
            previousRight = right;
            smp.cant = CantOfSample(smp, right);
        }
        const Vec3 nodeTangent = (ita != graphNodes.nodes.end()) ? ita->second.tangent : Vec3(0, 0, 1);
        edge.nodeATangentSign = rs::world::EdgeTangentSignAgainst(nodeTangent, edge.spline.front().tangent);
        rs::world::DeriveEdgeNodeBTangentSign(graphNodes, edge);
    }

    // RailWay1本の値を1:1で載せるだけで、伝播(ラッチ展開)は行わない
    // (値の無いRailWayが内部に散在し、
    // 分岐下流では走査順で継承元が変わるため取り込み時点で正解が定まらない)。
    edge.speedLimitKmh = way.speedLimit;
    // curveはBuildCurveFromSplitters3Dで復元した直線/3D円弧(Splitter隣接ペアごとに
    // 1本ずつ、圧縮しない密な方式。Rs2CurveFit.h参照)。RailSim II は
    // 緩和曲線を持たないためTransitionは生成されない。spline側は
    // 直後の再構築で作り直す(ここでのSplitter由来の値はカントの復元の入力だけに使う)。
    // refit.available は「両端ノード接線だけから単一3点フィレットとして再現できるか」を
    // IsThreePointFilletRepresentableで判定して決める(純円弧・片側のみ直線を持つ
    // edgeのみtrueになり、緩和曲線の後編集対象になる。前後とも直線を持つ非対称な
    // 直線+円弧+直線、複数円弧のS字等の複合曲線は、3点フィレットソルバの値域外
    // または乖離が閾値を超えるため自動的にfalseのまま=後編集不可、Rs2CurveFit.h参照)。
    edge.curve = BuildCurveFromSplitters3D(way.splitList);
    if (ita != graphNodes.nodes.end() && itb != graphNodes.nodes.end() &&
        IsThreePointFilletRepresentable(edge, ita->second, itb->second)) {
        edge.refit.shape = rs::world::AlignmentSegment::TransitionShape::Clothoid;
        edge.refit.transitionLength = 0.0f;
        edge.refit.available = true;
    }
    // Splitter.Up の焼き込み値(曲線の途中の増減)を制御点として持つ。弧長はsplineの長さから
    // 復元したcurveの長さへ合わせる(形の再構築後のcurve長とは数mmの差しか出ない)。
    // 両端を結ぶ線形で足りる(=途中の増減が無い)edgeは空のままにして、両端の値だけで作る。
    if (edge.spline.size() >= 2) {
        std::vector<rs::world::CantControlPoint> dense;
        dense.reserve(edge.spline.size());
        const double splineLen = rs::world::EdgeLength(edge);
        const double curveLen = rs::world::AlignmentTotalLength(edge.curve);
        const float scale = (splineLen > rs::core::kZeroLengthEps && curveLen > rs::core::kZeroLengthEps)
                                ? static_cast<float>(curveLen / splineLen)
                                : 1.0f;
        for (const TrackNodeSample& smp : edge.spline) dense.push_back({smp.s * scale, smp.cant});
        edge.cantPoints = rs::world::ThinCantSamples(dense);
    }
    edge.centerlineOnly = way.centerlineOnly;
    edge.trackCount = way.trackCount;
    edge.trackSpacing = way.trackSpacing;
    // trackEdges(平均元の実レールN本)は.rs2側に関係の記録が無いため復元できない。
    // 復元対象=IsCenterlineOnlyEdge()による除外/TrackPluginPlacementのtrackNum routingのみ。
    AppendTrackPlugins(edge, way, installRootUtf8);

    // 形は出自を問わず両端ノードの3D姿勢から作る。通常の許容で作れなければ、.rsl読込と同じく
    // 許容をゆるめ、それでも作れなければ両端を結ぶ直線で代用する。Splitter由来のsplineは
    // カントの復元とこの再構築の入力(curve/refit)にだけ使い、作れたあとは残さない。
    const rs::world::EdgeRebuildMethod method = rs::world::RebuildEdgeSplineWithFallback(graphNodes, edge);
    if (method == rs::world::EdgeRebuildMethod::Failed) {
        warnings.push_back(std::string(kShapeBuildFallbackWarning) + ": RailWay " + way.address +
                           " (作れないため取り込みスキップ)");
        return TrackEdge{};  // nodeA/nodeBがNilGuidのまま。呼び出し側がスキップする
    }
    if (method != rs::world::EdgeRebuildMethod::Exact) {
        warnings.push_back(std::string(kShapeBuildFallbackWarning) + ": RailWay " + way.address);
    }

    // PierLink/PoleLinkから厳密配置を構築する。オフセットは再構築後の形を基準にする
    // (以後のcurve編集はこの形に対する同じsの再評価で追従する)。
    AppendImportedPluginLinks(edge.importedPiers, edge, way.pierLinks, installRootUtf8, "Pier",
                              famIndex.pierByAddress, famIndex.pierPrimaryWay, way.address,
                              warnings);
    AppendImportedPluginLinks(edge.importedPoles, edge, way.poleLinks, installRootUtf8, "Pole",
                              famIndex.poleByAddress, famIndex.polePrimaryWay, way.address,
                              warnings);
    return edge;
}

} // namespace rs::legacy_layout

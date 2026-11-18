#include "SceneRouteReservationPhase.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "../../world/alignment/PointClaims.h"
#include "../../world/alignment/PointRoutineResolve.h"
#include "../../world/alignment/RailLinkOverlay.h"
#include "../../world/alignment/SingleTrackSection.h"
#include "../../world/alignment/SpeedSignalForwardScan.h"
#include "../../world/alignment/SpeedSignalRegistry.h"
#include "../../world/alignment/TrackGraph.h"

namespace rs::app {
namespace {

using ConstGraphForScene = std::function<const rs::world::TrackGraph*(size_t)>;

// 車体の後ろにこれだけ余裕を見てから、ポイントを手放す。
constexpr double kBodyMarginM = 5.0;
// 単線区間の予約を止める入口の信号を、信号のこの距離[m]先として扱う(信号より手前を指さないため)。
constexpr double kEntranceSignalMarginM = 0.001;
// 車体の範囲がedgeの端に届いたとみなす許容[m]。
constexpr double kEdgeEndToleranceM = 1e-3;

// 先読みの範囲に入った信号が守る区間の終わり(その先の信号まで)の、走査開始位置からの距離[m]。
// 範囲に信号が無ければ範囲そのまま。
double ClaimRangeM(const ConstGraphForScene& graphForScene, const rs::world::WarpLinkTable& warpTable,
                   const rs::world::ForwardScanSpan& forwardSpan, double seekAheadM) {
    const std::vector<rs::world::SpeedSignalAhead> signals = rs::world::CollectSignalsAhead(
        graphForScene, forwardSpan, +1, rs::world::NilGuid(), seekAheadM);
    if (signals.empty()) return seekAheadM;
    const rs::world::SpeedSignalAhead& last = signals.back();
    const rs::world::TrackGraph* graph = graphForScene(last.sceneIndex);
    if (graph == nullptr) return seekAheadM;
    const rs::world::SpeedSignal* signal = rs::world::FindSpeedSignal(*graph, last.signal);
    if (signal == nullptr) return seekAheadM;
    const rs::world::SpeedSignalForwardResult beyond = rs::world::SearchForwardSpeedSignalAcrossScenes(
        graphForScene, warpTable, last.sceneIndex,
        rs::world::TrackPosition{signal->edge, signal->s, signal->direction}, +1, signal->guid,
        rs::world::kSpeedSignalForwardSearchM);
    // 次の信号まで。無ければ閉塞が続くところまで(閉塞の外のポイントは求めない)。
    const double zoneLengthM =
        beyond.ahead.has_value() ? beyond.ahead->distanceM : beyond.coveredToM;
    return std::clamp(last.distanceM + zoneLengthM, seekAheadM,
                      std::max(seekAheadM, rs::world::kSpeedSignalForwardSearchM));
}

// ポイントの手前や上に編成の車体が残っているか。後尾がポイントを抜けるまでは手放さない。
// 車体の範囲(在線edge列から編成長ぶん)が、ポイントにつながるedgeのポイント側の端に届いていれば、残っている。
// 在線edge列が編成長に足りないとき(走り出したばかり)は、つながるedgeを踏んでいれば残っているとみなす。
bool TouchesBody(const rs::sim::HeldFace& face, const ConstGraphForScene& graphForScene,
                 const rs::sim::TrainOccupiedPath* occupiedPath, double trainLengthM) {
    if (occupiedPath == nullptr) return false;
    const rs::world::TrackGraph* graph = graphForScene(face.sceneIndex);
    if (graph == nullptr) return false;
    const rs::world::TrackNode* faceNode = rs::world::FindLinkNode(*graph, face.key.node);
    if (faceNode == nullptr) return false;
    const std::vector<rs::world::OccupiedRangeAcrossScenes> body = rs::sim::OccupiedRangesFromPath(
        *occupiedPath, graphForScene, trainLengthM + kBodyMarginM);
    for (const rs::world::TrackNode::Link& link : faceNode->links) {
        if (body.empty()) {
            for (const rs::sim::OccupiedPathEntry& entry : occupiedPath->entries) {
                if (entry.sceneIndex == face.sceneIndex && entry.edge == link.edge) return true;
            }
            continue;
        }
        const std::unordered_map<rs::world::Guid, rs::world::TrackEdge, rs::world::GuidHash>::const_iterator
            edgeIt = graph->edges.find(link.edge);
        if (edgeIt == graph->edges.end()) continue;
        const double edgeLengthM = rs::world::EdgeLength(edgeIt->second);
        for (const rs::world::OccupiedRangeAcrossScenes& range : body) {
            if (range.sceneIndex != face.sceneIndex || range.edge != link.edge) continue;
            if (link.end == 0 ? range.sBegin <= kEdgeEndToleranceM
                              : range.sEnd >= edgeLengthM - kEdgeEndToleranceM) {
                return true;
            }
        }
    }
    return false;
}

// 予約の対象にする距離の窓[fromM, toM](走査開始位置からの距離)。信号が守る区間のポイントだけを
// 対象にして、閉塞の外のポイントは対象にしない。
// 編成が閉塞の中にいるなら、編成の位置から閉塞が続くところまで。外にいる(閉塞の手前)なら、
// 最初の信号から閉塞が続くところまで。外にいて信号も無ければ空(toM=0)。
struct ClaimWindow {
    double fromM = 0.0;
    double toM = 0.0;
};

ClaimWindow ComputeClaimWindow(const ConstGraphForScene& graphForScene,
                               const rs::world::WarpLinkTable& warpTable,
                               const rs::world::ForwardScanSpan& wideSpan, double rangeM) {
    ClaimWindow window;
    if (wideSpan.entries.empty()) return window;
    const rs::world::EdgeAheadAcrossScenes& head = wideSpan.entries.front();
    const rs::world::TrackGraph* headGraph = graphForScene(head.sceneIndex);
    if (headGraph != nullptr && headGraph->edgeToBlocks.contains(head.edge)) {
        window.toM = rs::world::ContiguousBlockCoveredM(graphForScene, wideSpan);
        return window;
    }
    const std::vector<rs::world::SpeedSignalAhead> signals = rs::world::CollectSignalsAhead(
        graphForScene, wideSpan, +1, rs::world::NilGuid(), rangeM);
    if (signals.empty()) return window;
    const rs::world::SpeedSignalAhead& first = signals.front();
    const rs::world::TrackGraph* graph = graphForScene(first.sceneIndex);
    const rs::world::SpeedSignal* signal =
        graph != nullptr ? rs::world::FindSpeedSignal(*graph, first.signal) : nullptr;
    if (signal == nullptr) return window;
    const rs::world::SpeedSignalForwardResult fromFirst = rs::world::SearchForwardSpeedSignalAcrossScenes(
        graphForScene, warpTable, first.sceneIndex,
        rs::world::TrackPosition{signal->edge, signal->s, signal->direction}, +1, signal->guid,
        rs::world::kSpeedSignalForwardSearchM);
    window.fromM = first.distanceM;
    window.toM = first.distanceM + fromFirst.coveredToM;
    return window;
}

// spanが通る単線区間ごとに、向きの予約の要求を返す。区間の入口の信号が止める位置になるよう、
// 距離は入口の信号のすぐ先にする(信号が無ければ区間に入る位置)。farthestEndMには、
// 区間の終わりまでの距離の上限を返す。
std::vector<rs::world::PointClaim> CollectSectionClaims(
    const ConstGraphForScene& graphForScene, const rs::world::WarpLinkTable& warpTable,
    const rs::world::ForwardScanSpan& span, double toM, double& farthestEndM) {
    std::vector<rs::world::PointClaim> claims;
    const std::vector<rs::world::SpeedSignalAhead> signals = rs::world::CollectSignalsAhead(
        graphForScene, span, +1, rs::world::NilGuid(), toM);
    std::unordered_set<rs::world::Guid, rs::world::GuidHash> seenSections;
    std::unordered_set<rs::world::Guid, rs::world::GuidHash> coveredEdges;
    for (const rs::world::EdgeAheadAcrossScenes& entry : span.entries) {
        if (entry.distance > toM) break;
        if (coveredEdges.contains(entry.edge)) continue;
        const rs::world::TrackGraph* graph = graphForScene(entry.sceneIndex);
        if (graph == nullptr) continue;
        const std::optional<rs::world::SingleTrackSection> section = rs::world::FindSingleTrackSection(
            *graph, &warpTable, entry.sceneIndex, entry.edge, &coveredEdges);
        if (!section.has_value()) continue;
        if (!seenSections.insert(section->id).second) continue;
        rs::world::PointClaim claim;
        claim.sceneIndex = entry.sceneIndex;
        claim.node = section->id;
        claim.face = rs::world::kSectionClaimFace;
        claim.slot = rs::world::SectionTravelSlot(*section, entry.edge, entry.alongEdgeDir);
        claim.distanceM = entry.distance;
        for (const rs::world::SpeedSignalAhead& signal : signals) {
            if (signal.distanceM >= entry.distance) {
                claim.distanceM = signal.distanceM + kEntranceSignalMarginM;
                break;
            }
        }
        claims.push_back(claim);
        farthestEndM = std::max(farthestEndM, entry.distance + section->lengthM);
    }
    return claims;
}

// 車体が重なっている単線区間のid。
std::unordered_set<rs::world::Guid, rs::world::GuidHash> BodySectionIds(
    const ConstGraphForScene& graphForScene, const rs::world::WarpLinkTable& warpTable,
    const rs::sim::TrainOccupiedPath* occupiedPath, double trainLengthM) {
    std::unordered_set<rs::world::Guid, rs::world::GuidHash> ids;
    if (occupiedPath == nullptr) return ids;
    std::unordered_set<rs::world::Guid, rs::world::GuidHash> coveredEdges;
    const auto addSectionOf = [&](size_t sceneIndex, const rs::world::Guid& edge) {
        if (coveredEdges.contains(edge)) return;
        const rs::world::TrackGraph* graph = graphForScene(sceneIndex);
        if (graph == nullptr) return;
        const std::optional<rs::world::SingleTrackSection> section =
            rs::world::FindSingleTrackSection(*graph, &warpTable, sceneIndex, edge, &coveredEdges);
        if (section.has_value()) ids.insert(section->id);
    };
    const std::vector<rs::world::OccupiedRangeAcrossScenes> body = rs::sim::OccupiedRangesFromPath(
        *occupiedPath, graphForScene, trainLengthM + kBodyMarginM);
    if (body.empty()) {
        for (const rs::sim::OccupiedPathEntry& entry : occupiedPath->entries) {
            addSectionOf(entry.sceneIndex, entry.edge);
        }
    }
    for (const rs::world::OccupiedRangeAcrossScenes& range : body) addSectionOf(range.sceneIndex, range.edge);
    return ids;
}

} // namespace

std::unordered_set<rs::world::Guid, rs::world::GuidHash> RunRouteReservationPhase(
    rs::sim::RouteReservation& ledger, const rs::sim::TrainRuntime& runtime,
    const std::function<rs::world::TrackGraph*(size_t)>& graphForScene,
    const rs::world::WarpLinkTable& warpTable, TrainId trainId,
    const rs::world::ForwardScanSpan& forwardSpan, double seekAheadM,
    const rs::world::NodeSlotOverride* slotOverride,
    const std::unordered_set<rs::world::Guid, rs::world::GuidHash>& blocksOccupiedByOthers,
    double trainLengthM, double nowAbsSeconds) {
    std::unordered_set<rs::world::Guid, rs::world::GuidHash> denied;
    const ConstGraphForScene constGraphForScene = [&graphForScene](size_t sceneIndex) {
        return static_cast<const rs::world::TrackGraph*>(graphForScene(sceneIndex));
    };
    rs::sim::LeadingEnd leadingEnd;
    if (!runtime.GetGroupForwardScanStart(constGraphForScene, warpTable, trainId, leadingEnd)) {
        return denied;
    }

    // 範囲に入った信号が守る区間の終わりまで、進路が通るポイントを求める。
    double claimRangeM = ClaimRangeM(constGraphForScene, warpTable, forwardSpan, seekAheadM);
    // 単線区間は、入口の向きと出口のポイントを1回の予約で取れるよう、区間の終わりまで範囲を広げる。
    double sectionEndM = 0.0;
    CollectSectionClaims(constGraphForScene, warpTable, forwardSpan, seekAheadM, sectionEndM);
    claimRangeM = std::clamp(std::max(claimRangeM, sectionEndM), seekAheadM,
                             std::max(seekAheadM, rs::world::kSpeedSignalForwardSearchM));
    rs::world::ForwardScanSpan scanned;
    if (claimRangeM > seekAheadM) {
        scanned = rs::world::ScanForwardSpanAcrossScenes(constGraphForScene, warpTable,
                                                         leadingEnd.sceneIndex, leadingEnd.position,
                                                         claimRangeM, slotOverride);
    }
    const rs::world::ForwardScanSpan& wideSpan = claimRangeM > seekAheadM ? scanned : forwardSpan;
    // 信号が守る区間のポイントだけ。分岐で入るポイントは、開通先を決めた範囲の中だけ。
    const ClaimWindow window = ComputeClaimWindow(constGraphForScene, warpTable, wideSpan, claimRangeM);
    std::vector<rs::world::PointClaim> reachable = rs::world::CollectPointClaims(
        constGraphForScene, wideSpan, std::min(claimRangeM, window.toM));
    reachable.erase(std::remove_if(reachable.begin(), reachable.end(),
                                   [&](const rs::world::PointClaim& claim) {
                                       return claim.distanceM < window.fromM ||
                                              (!claim.trailing && claim.distanceM > seekAheadM);
                                   }),
                    reachable.end());
    double ignoredEndM = 0.0;
    for (const rs::world::PointClaim& claim : CollectSectionClaims(
             constGraphForScene, warpTable, wideSpan, std::min(claimRangeM, window.toM), ignoredEndM)) {
        if (claim.distanceM >= window.fromM) reachable.push_back(claim);
    }
    for (rs::world::PointClaim& claim : reachable) {
        const rs::world::TrackGraph* graph = constGraphForScene(claim.sceneIndex);
        if (graph == nullptr) continue;
        const rs::world::TrackNode* node = rs::world::FindLinkNode(*graph, claim.node);
        if (node == nullptr) continue;
        claim.blockedByOccupancy =
            node->activeSlot[static_cast<size_t>(claim.face)] != claim.slot &&
            rs::world::IsNodeAdjacentToOccupiedBlock(*graph, *node, blocksOccupiedByOthers);
    }

    // 駅の停止目標より先のポイントは取りに行かない(停車中の列車が押さえ続けないため)。
    // 信号を止めるかどうかの判定には含める(停車中は、発車するまで出発の信号が止まる)。
    const std::optional<double> stopDistanceM = runtime.GroupStopDistanceM(trainId);
    std::vector<rs::world::PointClaim> claims = reachable;
    if (stopDistanceM.has_value()) {
        claims.erase(std::remove_if(claims.begin(), claims.end(),
                                    [&](const rs::world::PointClaim& claim) {
                                        return claim.distanceM > *stopDistanceM;
                                    }),
                     claims.end());
    }

    // 取れたら、ポイントをその線へ切り替える。切り替えられないポイント(隣の閉塞に他編成)は触らない。
    if (ledger.Acquire(trainId, claims, nowAbsSeconds)) {
        for (const rs::world::PointClaim& claim : claims) {
            if (claim.blockedByOccupancy) continue;
            rs::world::TrackGraph* graph = graphForScene(claim.sceneIndex);
            if (graph == nullptr) continue;
            rs::world::TrackNode* node = rs::world::FindLinkNodeMutable(*graph, claim.node);
            if (node == nullptr) continue;
            node->activeSlot[static_cast<size_t>(claim.face)] = claim.slot;
        }
    }

    // 求める範囲から外れ、編成の在線からも外れたポイントを手放す。
    std::unordered_set<rs::sim::NodeFaceKey, rs::sim::NodeFaceKeyHash> keep;
    for (const rs::world::PointClaim& claim : claims) keep.insert(rs::sim::KeyOf(claim));
    const rs::sim::TrainOccupiedPath* occupiedPath = runtime.GroupOccupiedPath(trainId);
    const std::unordered_set<rs::world::Guid, rs::world::GuidHash> bodySections =
        BodySectionIds(constGraphForScene, warpTable, occupiedPath, trainLengthM);
    for (const rs::sim::HeldFace& face : ledger.HeldFaces(trainId)) {
        const bool touchesBody = face.key.face == rs::world::kSectionClaimFace
                                     ? bodySections.contains(face.key.node)
                                     : TouchesBody(face, constGraphForScene, occupiedPath, trainLengthM);
        if (touchesBody) keep.insert(face.key);
    }
    ledger.ReleaseExcept(trainId, keep);

    // まだ取れていないポイントを区間に持つ信号を、この編成には停止にする。
    const std::vector<rs::world::SpeedSignalAhead> signals = rs::world::CollectSignalsAhead(
        constGraphForScene, wideSpan, +1, rs::world::NilGuid(), claimRangeM);
    for (const rs::world::PointClaim& claim : reachable) {
        if (ledger.Holds(trainId, claim)) continue;
        const std::vector<rs::world::SpeedSignalAhead>::const_iterator after = std::find_if(
            signals.begin(), signals.end(), [&](const rs::world::SpeedSignalAhead& signal) {
                return signal.distanceM >= claim.distanceM;
            });
        if (after != signals.begin()) denied.insert(std::prev(after)->signal);
    }
    return denied;
}

} // namespace rs::app

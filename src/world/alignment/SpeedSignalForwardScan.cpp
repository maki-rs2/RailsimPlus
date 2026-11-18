#include "SpeedSignalForwardScan.h"

#include <algorithm>

#include "BlockRegistry.h"
#include "PointClaims.h"
#include "RailLinkOverlay.h"
#include "SpeedSignalRegistry.h"
#include "TrackEdge.h"
#include "TrackGraph.h"
#include "TrackTraversal.h"

namespace rs::world {

namespace {

// edgeがいずれかの閉塞に入っているか(閉塞はedge全長を覆う)。
bool IsCoveredByBlocks(const TrackGraph& graph, const Guid& edge) {
    return !FindBlocksOnEdge(graph, edge).empty();
}

// span上の走査開始位置から距離distanceMまでの線路が、途切れなく閉塞に入っているか。
bool IsPathCoveredByBlocks(const std::function<const TrackGraph*(size_t)>& graphForScene,
                           const ForwardScanSpan& span, double distanceM) {
    for (const EdgeAheadAcrossScenes& entry : span.entries) {
        if (entry.distance >= distanceM) break;
        const TrackGraph* graph = graphForScene(entry.sceneIndex);
        if (graph == nullptr) return false;
        const std::unordered_map<Guid, TrackEdge, GuidHash>::const_iterator edgeIt =
            graph->edges.find(entry.edge);
        if (edgeIt == graph->edges.end()) return false;
        const double travelM = distanceM - entry.distance;
        const double edgeLengthM = EdgeLength(edgeIt->second);
        const double endS = entry.alongEdgeDir ? std::min(edgeLengthM, entry.entryS + travelM)
                                               : std::max(0.0, entry.entryS - travelM);
        // 走る距離が0のedgeは通らないのと同じなので数えない。
        if (endS == entry.entryS) continue;
        if (!IsCoveredByBlocks(*graph, entry.edge)) return false;
    }
    return true;
}

// spanの先頭からlimitMまでの間に、合流で入るポイントが入ってくる線に開通していないところがあるか。
// 閉塞の外のポイントは数えない(呼び出し側がlimitMを閉塞が続く距離までに抑える)。
bool HasUnalignedTrailingPoint(const std::function<const TrackGraph*(size_t)>& graphForScene,
                               const ForwardScanSpan& span, double limitM) {
    for (const PointClaim& claim : CollectPointClaims(graphForScene, span, limitM)) {
        if (!claim.trailing) continue;
        const TrackGraph* graph = graphForScene(claim.sceneIndex);
        if (graph == nullptr) continue;
        const TrackNode* node = FindLinkNode(*graph, claim.node);
        if (node == nullptr) continue;
        if (node->activeSlot[static_cast<size_t>(claim.face)] != claim.slot) return true;
    }
    return false;
}

// 前方の信号として数えられる候補か。向き・除外・担当閉塞の有無を1か所で判定する。
// travelDirは走査中のedge内の進行方向(+1=edgeの向き / -1=逆)。
bool IsForwardCandidate(const TrackGraph& graph, const SpeedSignal& candidate,
                        const Guid& excludeGuid, int relativeSign, int travelDir) {
    if (candidate.guid == excludeGuid) return false;
    if (!rs::world::IsBlockBoundarySignal(candidate.kind)) return false;
    if (candidate.direction != relativeSign * travelDir) return false;
    return OwnerBlock(graph, candidate) != nullptr;
}

} // namespace

double ContiguousBlockCoveredM(const std::function<const TrackGraph*(size_t)>& graphForScene,
                               const ForwardScanSpan& span) {
    for (const EdgeAheadAcrossScenes& entry : span.entries) {
        const TrackGraph* graph = graphForScene(entry.sceneIndex);
        if (graph == nullptr) return entry.distance;
        const std::unordered_map<Guid, TrackEdge, GuidHash>::const_iterator edgeIt =
            graph->edges.find(entry.edge);
        if (edgeIt == graph->edges.end()) return entry.distance;
        if (!IsCoveredByBlocks(*graph, entry.edge)) return entry.distance;
    }
    return span.scannedM;
}

std::vector<SpeedSignalAhead> CollectSignalsAhead(
    const std::function<const TrackGraph*(size_t)>& graphForScene, const ForwardScanSpan& span,
    int relativeSign, const Guid& excludeGuid, double maxSearchM) {
    std::vector<SpeedSignalAhead> signals;
    for (const EdgeAheadAcrossScenes& entry : span.entries) {
        const TrackGraph* graph = graphForScene(entry.sceneIndex);
        if (graph == nullptr) continue;
        const std::unordered_map<Guid, std::vector<size_t>, GuidHash>::const_iterator signalIt =
            graph->edgeToSpeedSignals.find(entry.edge);
        if (signalIt == graph->edgeToSpeedSignals.end()) continue;
        const int travelDir = entry.alongEdgeDir ? 1 : -1;
        for (const size_t signalIndex : signalIt->second) {
            if (signalIndex >= graph->speedSignals.size()) continue;
            const SpeedSignal& candidate = graph->speedSignals[signalIndex];
            if (!IsForwardCandidate(*graph, candidate, excludeGuid, relativeSign, travelDir)) {
                continue;
            }
            if ((candidate.s - entry.entryS) * travelDir <= 0.0) continue;
            const double distanceM = entry.distance + (candidate.s - entry.entryS) * travelDir;
            if (distanceM > maxSearchM) continue;
            signals.push_back(SpeedSignalAhead{entry.sceneIndex, candidate.guid, distanceM});
        }
    }
    std::sort(signals.begin(), signals.end(),
              [](const SpeedSignalAhead& a, const SpeedSignalAhead& b) { return a.distanceM < b.distanceM; });
    return signals;
}

std::optional<SpeedSignalAhead> FindNearestSpeedSignalAlongAcrossScenes(
    const std::function<const TrackGraph*(size_t)>& graphForScene, const WarpLinkTable& warpTable,
    size_t startScene, TrackPosition scanStart, int relativeSign, const Guid& excludeGuid,
    double maxSearchM, const NodeSlotOverride* slotOverride) {
    return SearchForwardSpeedSignalAcrossScenes(graphForScene, warpTable, startScene, scanStart,
                                                relativeSign, excludeGuid, maxSearchM, slotOverride)
        .ahead;
}

std::optional<SpeedSignalAhead> FindNearestSpeedSignalAlongAcrossScenes(
    const std::function<const TrackGraph*(size_t)>& graphForScene, const WarpLinkTable& warpTable,
    size_t startScene, TrackPosition scanStart, int relativeSign, const Guid& excludeGuid,
    double maxSearchM, const ForwardScanSpan& span, const NodeSlotOverride* slotOverride) {
    return SearchForwardSpeedSignalAcrossScenes(graphForScene, warpTable, startScene, scanStart,
                                                relativeSign, excludeGuid, maxSearchM, span,
                                                slotOverride)
        .ahead;
}

SpeedSignalForwardResult SearchForwardSpeedSignalAcrossScenes(
    const std::function<const TrackGraph*(size_t)>& graphForScene, const WarpLinkTable& warpTable,
    size_t startScene, TrackPosition scanStart, int relativeSign, const Guid& excludeGuid,
    double maxSearchM, const NodeSlotOverride* slotOverride) {
    const ForwardScanSpan span = ScanForwardSpanAcrossScenes(graphForScene, warpTable, startScene,
                                                              scanStart, maxSearchM, slotOverride);
    return SearchForwardSpeedSignalAcrossScenes(graphForScene, warpTable, startScene, scanStart,
                                                relativeSign, excludeGuid, maxSearchM, span,
                                                slotOverride);
}

SpeedSignalForwardResult SearchForwardSpeedSignalAcrossScenes(
    const std::function<const TrackGraph*(size_t)>& graphForScene, const WarpLinkTable& warpTable,
    size_t startScene, TrackPosition scanStart, int relativeSign, const Guid& excludeGuid,
    double maxSearchM, const ForwardScanSpan& span, const NodeSlotOverride* slotOverride) {
    if (maxSearchM > span.scannedM && !span.termination.atEnd) {
        const ForwardScanSpan fallback = ScanForwardSpanAcrossScenes(
            graphForScene, warpTable, startScene, scanStart, maxSearchM, slotOverride);
        return SearchForwardSpeedSignalAcrossScenes(graphForScene, warpTable, startScene, scanStart,
                                                    relativeSign, excludeGuid, maxSearchM, fallback,
                                                    slotOverride);
    }
    const std::vector<SpeedSignalAhead> candidates =
        CollectSignalsAhead(graphForScene, span, relativeSign, excludeGuid, maxSearchM);
    std::optional<SpeedSignalAhead> nearest;
    if (!candidates.empty()) nearest = candidates.front();
    SpeedSignalForwardResult result;
    result.coveredToM = ContiguousBlockCoveredM(graphForScene, span);
    result.routeNotAligned = HasUnalignedTrailingPoint(
        graphForScene, span,
        std::min(result.coveredToM, nearest.has_value() ? nearest->distanceM : span.scannedM));
    if (nearest.has_value()) {
        // 閉塞に入っていない線路をはさんだ先の信号は、前方として参照しない(何も無い扱い)。
        if (IsPathCoveredByBlocks(graphForScene, span, nearest->distanceM)) result.ahead = nearest;
        return result;
    }
    // 信号が無いとき、閉塞が途切れずに行き止まりまで続くなら、行き止まりを前方の赤とみなす。
    result.endsAtDeadEnd = !span.entries.empty() &&
                           span.termination.reason == ScanEndReason::DeadEnd &&
                           result.coveredToM >= span.scannedM;
    return result;
}

} // namespace rs::world

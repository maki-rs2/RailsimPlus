#include "WarpForwardScan.h"

#include <algorithm>
#include <cmath>

#include "BlockSpeedLimit.h"
#include "RailLinkOverlay.h"
#include "TrackEdge.h"
#include "TrackGraph.h"
#include "TrackTraversal.h"

namespace rs::world {

ForwardScanSpan ScanForwardSpanAcrossScenes(
    const std::function<const TrackGraph*(size_t)>& graphForScene,
    const WarpLinkTable& warpTable, size_t startScene, TrackPosition start, double maxSearchM,
    const NodeSlotOverride* slotOverride) {
    ForwardScanSpan result;
    result.entries = ScanEdgesAheadAcrossScenes(graphForScene, warpTable, startScene, start,
                                                maxSearchM, &result.termination, slotOverride);
    if (result.termination.exceededMax) {
        result.scannedM = maxSearchM;
    } else if (!result.entries.empty()) {
        const EdgeAheadAcrossScenes& last = result.entries.back();
        const TrackGraph* graph = graphForScene(last.sceneIndex);
        if (graph != nullptr) {
            const std::unordered_map<Guid, TrackEdge, GuidHash>::const_iterator edgeIt =
                graph->edges.find(last.edge);
            if (edgeIt != graph->edges.end()) {
                const double edgeLength = EdgeLength(edgeIt->second);
                const double remainingM = last.alongEdgeDir ? edgeLength - last.entryS : last.entryS;
                result.scannedM = std::min(maxSearchM, last.distance + std::max(0.0, remainingM));
            }
        }
    }
    return result;
}

std::vector<EdgeAheadAcrossScenes> ScanEdgesAheadAcrossScenes(
    const std::function<const TrackGraph*(size_t)>& graphForScene,
    const WarpLinkTable& warpTable, size_t startScene, TrackPosition start, double maxSearchM,
    WarpScanTermination* outTermination, const NodeSlotOverride* slotOverride) {
    WarpScanTermination termination;
    std::vector<EdgeAheadAcrossScenes> result;
    if (!std::isfinite(maxSearchM) || maxSearchM < 0.0) {
        termination.atEnd = true;
        termination.reason = ScanEndReason::DeadEnd;
        if (outTermination != nullptr) *outTermination = termination;
        return result;
    }

    size_t sceneIndex = startScene;
    TrackPosition pos = start;
    double accumulated = 0.0;
    int warpHops = 0;
    constexpr int kMaxHops = 4096;
    constexpr int kMaxWarpHops = 64;
    for (int hop = 0; hop <= kMaxHops && accumulated <= maxSearchM; ++hop) {
        const TrackGraph* graph = graphForScene(sceneIndex);
        if (graph == nullptr) {
            termination.atEnd = true;
            termination.reason = ScanEndReason::DeadEnd;
            break;
        }
        const std::unordered_map<Guid, TrackEdge, GuidHash>::const_iterator edgeIt = graph->edges.find(pos.edge);
        if (edgeIt == graph->edges.end()) {
            termination.atEnd = true;
            termination.reason = ScanEndReason::DeadEnd;
            break;
        }
        const TrackEdge& edge = edgeIt->second;
        result.push_back(
            EdgeAheadAcrossScenes{sceneIndex, pos.edge, accumulated, pos.direction >= 0, pos.s, warpHops});

        const double edgeLength = EdgeLength(edge);
        accumulated += pos.direction >= 0 ? edgeLength - pos.s : pos.s;
        if (accumulated > maxSearchM) { termination.exceededMax = true; break; }
        const Guid endNode = LinkNodeOfEnd(*graph, edge, pos.direction >= 0 ? 1 : 0);
        const TrackNode* node = FindLinkNode(*graph, endNode);
        if (node == nullptr) {
            termination.atEnd = true;
            termination.reason = ScanEndReason::DeadEnd;
            break;
        }
        const LinkSelection selection =
            SelectNextRealLink(*graph, *node, pos.edge, slotOverride);
        if (selection.next.has_value()) {
            const TrackNode::Link& next = *selection.next;
            const std::unordered_map<Guid, TrackEdge, GuidHash>::const_iterator nextEdgeIt = graph->edges.find(next.edge);
            if (nextEdgeIt == graph->edges.end()) {
                termination.atEnd = true;
                termination.reason = ScanEndReason::DeadEnd;
                break;
            }
            pos.edge = next.edge;
            pos.s = next.end == 0 ? 0.0 : EdgeLength(nextEdgeIt->second);
            pos.direction = next.end == 0 ? +1 : -1;
            continue;
        }
        if (warpHops >= kMaxWarpHops) {
            termination.atEnd = true;
            termination.reason = ScanEndReason::SceneBoundary;
            break;
        }
        const std::optional<WarpEndpoint> other = warpTable.Find(sceneIndex, endNode);
        if (!other.has_value()) {
            termination.atEnd = true;
            termination.reason = ScanEndReason::DeadEnd;
            break;
        }
        const TrackGraph* otherGraph = graphForScene(other->sceneIndex);
        if (otherGraph == nullptr) {
            termination.atEnd = true;
            termination.reason = ScanEndReason::SceneBoundary;
            break;
        }
        const TrackNode* otherNode = FindLinkNode(*otherGraph, other->node);
        if (otherNode == nullptr || otherNode->links.empty()) {
            termination.atEnd = true;
            termination.reason = ScanEndReason::SceneBoundary;
            break;
        }
        const TrackNode::Link& next = otherNode->links[0];
        const std::unordered_map<Guid, TrackEdge, GuidHash>::const_iterator nextEdgeIt = otherGraph->edges.find(next.edge);
        if (nextEdgeIt == otherGraph->edges.end()) {
            termination.atEnd = true;
            termination.reason = ScanEndReason::SceneBoundary;
            break;
        }
        sceneIndex = other->sceneIndex;
        ++warpHops;
        pos.edge = next.edge;
        pos.s = next.end == 0 ? 0.0 : EdgeLength(nextEdgeIt->second);
        pos.direction = next.end == 0 ? +1 : -1;
    }
    if (!termination.atEnd && !termination.exceededMax) termination.atEnd = true;
    if (outTermination != nullptr) *outTermination = termination;
    return result;
}

std::vector<OccupiedRangeAcrossScenes> ScanOccupiedRangesAheadAcrossScenes(
    const std::function<const TrackGraph*(size_t)>& graphForScene,
    const WarpLinkTable& warpTable, size_t startScene, TrackPosition start, double maxSearchM,
    const NodeSlotOverride* slotOverride) {
    const ForwardScanSpan span = ScanForwardSpanAcrossScenes(graphForScene, warpTable, startScene,
                                                              start, maxSearchM, slotOverride);
    return ScanOccupiedRangesAheadAcrossScenes(graphForScene, warpTable, startScene, start,
                                               maxSearchM, span, slotOverride);
}

std::vector<OccupiedRangeAcrossScenes> ScanOccupiedRangesAheadAcrossScenes(
    const std::function<const TrackGraph*(size_t)>& graphForScene,
    const WarpLinkTable& warpTable, size_t startScene, TrackPosition start, double maxSearchM,
    const ForwardScanSpan& span, const NodeSlotOverride* slotOverride) {
    if (maxSearchM > span.scannedM && !span.termination.atEnd) {
        const ForwardScanSpan fallback = ScanForwardSpanAcrossScenes(
            graphForScene, warpTable, startScene, start, maxSearchM, slotOverride);
        return ScanOccupiedRangesAheadAcrossScenes(graphForScene, warpTable, startScene, start,
                                                   maxSearchM, fallback, slotOverride);
    }
    std::vector<OccupiedRangeAcrossScenes> result;
    const std::vector<EdgeAheadAcrossScenes>& entries = span.entries;
    for (size_t i = 0; i < entries.size(); ++i) {
        const TrackGraph* graph = graphForScene(entries[i].sceneIndex);
        if (graph == nullptr) continue;
        const std::unordered_map<Guid, TrackEdge, GuidHash>::const_iterator edgeIt = graph->edges.find(entries[i].edge);
        if (edgeIt == graph->edges.end()) continue;
        const double edgeLength = EdgeLength(edgeIt->second);
        const double distanceIntoEdge = (i + 1 < entries.size() ? entries[i + 1].distance : maxSearchM) - entries[i].distance;
        const double available = entries[i].alongEdgeDir ? edgeLength - entries[i].entryS : entries[i].entryS;
        const double occupiedLength = std::max(0.0, std::min(distanceIntoEdge, available));
        const double exitS = entries[i].entryS + (entries[i].alongEdgeDir ? occupiedLength : -occupiedLength);
        const double sBegin = std::min(entries[i].entryS, exitS);
        const double sEnd = std::max(entries[i].entryS, exitS);
        if (sEnd > sBegin) result.push_back(OccupiedRangeAcrossScenes{entries[i].sceneIndex, entries[i].edge, sBegin, sEnd});
    }
    return result;
}

std::vector<SpeedLimitAhead> ScanSpeedLimitsAheadAcrossScenes(
    const std::function<const TrackGraph*(size_t)>& graphForScene,
    const WarpLinkTable& warpTable, size_t startScene, TrackPosition start, double maxSearchM,
    const NodeSlotOverride* slotOverride, const SignalSpeedForTrainFn& signalSpeed) {
    const ForwardScanSpan span = ScanForwardSpanAcrossScenes(graphForScene, warpTable, startScene,
                                                              start, maxSearchM, slotOverride);
    return ScanSpeedLimitsAheadAcrossScenes(graphForScene, warpTable, startScene, start,
                                            maxSearchM, span, slotOverride, signalSpeed);
}

std::vector<SpeedLimitAhead> ScanSpeedLimitsAheadAcrossScenes(
    const std::function<const TrackGraph*(size_t)>& graphForScene,
    const WarpLinkTable& warpTable, size_t startScene, TrackPosition start, double maxSearchM,
    const ForwardScanSpan& span, const NodeSlotOverride* slotOverride,
    const SignalSpeedForTrainFn& signalSpeed) {
    if (maxSearchM > span.scannedM && !span.termination.atEnd) {
        const ForwardScanSpan fallback = ScanForwardSpanAcrossScenes(
            graphForScene, warpTable, startScene, start, maxSearchM, slotOverride);
        return ScanSpeedLimitsAheadAcrossScenes(graphForScene, warpTable, startScene, start,
                                                maxSearchM, fallback, slotOverride, signalSpeed);
    }
    std::vector<SpeedLimitAhead> result;
    const std::vector<EdgeAheadAcrossScenes>& entries = span.entries;
    for (const EdgeAheadAcrossScenes& entry : entries) {
        const TrackGraph* graph = graphForScene(entry.sceneIndex);
        if (graph == nullptr) continue;
        const std::unordered_map<Guid, TrackEdge, GuidHash>::const_iterator edgeIt = graph->edges.find(entry.edge);
        if (edgeIt == graph->edges.end()) continue;
        // edgeごとに1件。進行方向と同じ向きの信号の制限速度と重ねて小さい方が有効。
        const float limit = EffectiveSpeedLimitKmh(*graph, entry.edge, edgeIt->second,
                                                   entry.alongEdgeDir, signalSpeed, &warpTable,
                                                   entry.sceneIndex);
        result.push_back(SpeedLimitAhead{entry.distance, limit});
    }
    return result;
}

WarpForwardScanResult FindForwardEdgeAcrossScenes(
    const std::function<const TrackGraph*(size_t)>& graphForScene,
    const WarpLinkTable& warpTable, size_t startScene, TrackPosition start,
    const std::function<bool(size_t sceneIndex, const Guid& edge)>& isTarget,
    double maxSearchM, const NodeSlotOverride* slotOverride) {
    const ForwardScanSpan span = ScanForwardSpanAcrossScenes(graphForScene, warpTable, startScene,
                                                              start, maxSearchM, slotOverride);
    return FindForwardEdgeAcrossScenes(graphForScene, warpTable, startScene, start, isTarget,
                                       maxSearchM, span, slotOverride);
}

WarpForwardScanResult FindForwardEdgeAcrossScenes(
    const std::function<const TrackGraph*(size_t)>& graphForScene,
    const WarpLinkTable& warpTable, size_t startScene, TrackPosition start,
    const std::function<bool(size_t sceneIndex, const Guid& edge)>& isTarget,
    double maxSearchM, const ForwardScanSpan& span, const NodeSlotOverride* slotOverride) {
    if (maxSearchM > span.scannedM && !span.termination.atEnd) {
        const ForwardScanSpan fallback = ScanForwardSpanAcrossScenes(
            graphForScene, warpTable, startScene, start, maxSearchM, slotOverride);
        return FindForwardEdgeAcrossScenes(graphForScene, warpTable, startScene, start, isTarget,
                                           maxSearchM, fallback, slotOverride);
    }
    WarpForwardScanResult result;
    result.sceneIndex = startScene;
    result.entryPos = start;

    const std::vector<EdgeAheadAcrossScenes>& entries = span.entries;
    for (const EdgeAheadAcrossScenes& entry : entries) {
        if (isTarget(entry.sceneIndex, entry.edge)) {
            result.found = true;
            result.sceneIndex = entry.sceneIndex;
            result.foundEdge = entry.edge;
            result.distance = entry.distance;
            result.entryPos = TrackPosition{entry.edge, entry.entryS, entry.alongEdgeDir ? +1 : -1};
            result.warpHops = entry.warpHops;
            return result;
        }
    }
    result.atEnd = span.termination.atEnd;
    return result;
}

} // namespace rs::world

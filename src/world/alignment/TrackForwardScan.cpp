#include "TrackForwardScan.h"

#include <algorithm>
#include <cmath>

#include "RailLinkOverlay.h"

namespace rs::world {

ForwardScanResult FindForwardEdge(const TrackGraph& graph, TrackPosition start,
                                   const std::unordered_set<Guid, GuidHash>& targetEdges,
                                   double maxSearchM, const NodeSlotOverride* slotOverride) {
    ForwardScanResult result;
    ScanTermination termination;
    const std::vector<EdgeAhead> entries =
        ScanEdgesAhead(graph, start, maxSearchM, &termination, slotOverride);
    for (const EdgeAhead& entry : entries) {
        if (targetEdges.find(entry.edge) == targetEdges.end()) continue;
        result.found = true;
        result.foundEdge = entry.edge;
        result.distance = entry.distance;
        return result;
    }
    result.atEnd = termination.atEnd;
    result.exceededMax = termination.exceededMax;
    return result;
}

std::vector<EdgeAhead> ScanEdgesAhead(const TrackGraph& graph, TrackPosition start,
                                       double maxSearchM, ScanTermination* outTermination,
                                       const NodeSlotOverride* slotOverride) {
    ScanTermination termination;
    std::vector<EdgeAhead> result;
    if (!std::isfinite(maxSearchM) || maxSearchM < 0.0) {
        termination.atEnd = true;
        if (outTermination != nullptr) *outTermination = termination;
        return result;
    }

    TrackPosition pos = start;
    double accumulated = 0.0;
    constexpr int kMaxHops = 4096;
    for (int hop = 0; hop <= kMaxHops && accumulated <= maxSearchM; ++hop) {
        const std::unordered_map<Guid, TrackEdge, GuidHash>::const_iterator edgeIt =
            graph.edges.find(pos.edge);
        if (edgeIt == graph.edges.end()) {
            termination.atEnd = true;
            break;
        }
        const TrackEdge& edge = edgeIt->second;
        result.push_back(EdgeAhead{pos.edge, accumulated, pos.direction >= 0});

        const double edgeLengthM = EdgeLength(edge);
        const double toEndM = pos.direction >= 0 ? edgeLengthM - pos.s : pos.s;
        accumulated += toEndM;
        if (accumulated > maxSearchM) {
            termination.exceededMax = true;
            break;
        }

        const Guid endNode = LinkNodeOfEnd(graph, edge, pos.direction >= 0 ? 1 : 0);
        const TrackNode* node = FindLinkNode(graph, endNode);
        if (node == nullptr) {
            termination.atEnd = true;
            break;
        }
        const LinkSelection selection =
            SelectNextRealLink(graph, *node, pos.edge, slotOverride);
        if (!selection.next.has_value()) {
            termination.atEnd = true;
            break;
        }
        const TrackNode::Link& next = *selection.next;
        const std::unordered_map<Guid, TrackEdge, GuidHash>::const_iterator nextEdgeIt =
            graph.edges.find(next.edge);
        if (nextEdgeIt == graph.edges.end()) {
            termination.atEnd = true;
            break;
        }
        pos.edge = next.edge;
        pos.s = next.end == 0 ? 0.0 : EdgeLength(nextEdgeIt->second);
        pos.direction = next.end == 0 ? +1 : -1;
    }

    if (!termination.atEnd && !termination.exceededMax) termination.atEnd = true;
    if (outTermination != nullptr) *outTermination = termination;
    return result;
}

std::vector<OccupiedRange> ScanOccupiedRangesAhead(const TrackGraph& graph, TrackPosition start,
                                                     double maxSearchM,
                                                     const NodeSlotOverride* slotOverride) {
    std::vector<OccupiedRange> result;
    const std::vector<EdgeAhead> entries =
        ScanEdgesAhead(graph, start, maxSearchM, nullptr, slotOverride);
    for (size_t i = 0; i < entries.size(); ++i) {
        const std::unordered_map<Guid, TrackEdge, GuidHash>::const_iterator edgeIt =
            graph.edges.find(entries[i].edge);
        if (edgeIt == graph.edges.end()) continue;
        const double edgeLengthM = EdgeLength(edgeIt->second);
        const double entryS = i == 0 ? start.s : (entries[i].alongEdgeDir ? 0.0 : edgeLengthM);
        const double travelDir = entries[i].alongEdgeDir ? 1.0 : -1.0;
        const double distanceIntoThisEdgeM =
            (i + 1 < entries.size() ? entries[i + 1].distance : maxSearchM) - entries[i].distance;
        const double maxAvailableInEdgeM = travelDir > 0.0 ? edgeLengthM - entryS : entryS;
        const double occupiedLengthM =
            std::max(0.0, std::min(distanceIntoThisEdgeM, maxAvailableInEdgeM));
        const double exitS = entryS + travelDir * occupiedLengthM;
        const double sBegin = std::min(entryS, exitS);
        const double sEnd = std::max(entryS, exitS);
        if (sEnd - sBegin > 0.0) {
            result.push_back(OccupiedRange{entries[i].edge, sBegin, sEnd});
        }
    }
    return result;
}

bool OccupiedRangesOverlap(const std::vector<OccupiedRange>& a,
                           const std::vector<OccupiedRange>& b) {
    for (const OccupiedRange& aRange : a) {
        for (const OccupiedRange& bRange : b) {
            if (aRange.edge != bRange.edge) continue;
            if (std::max(aRange.sBegin, bRange.sBegin) < std::min(aRange.sEnd, bRange.sEnd)) {
                return true;
            }
        }
    }
    return false;
}

} // namespace rs::world

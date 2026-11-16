#include "TravelDirectionFollow.h"

#include <deque>
#include <unordered_map>
#include <vector>

#include "RailLinkOverlay.h"

namespace rs::world {

void CollectNeighbors(const TrackGraph& graph, const WarpLinkTable* warpTable, size_t sceneIndex,
                      const Guid& nodeGuid, const Guid& fromEdge, int fromEnd, bool arriving,
                      std::vector<DirectedEdge>& out) {
    const TrackNode* node = FindLinkNode(graph, nodeGuid);
    if (node == nullptr) return;
    int fromSide = -1;
    for (const TrackNode::Link& link : node->links) {
        if (link.edge == fromEdge && link.end == fromEnd) fromSide = link.side;
    }
    for (const TrackNode::Link& link : node->links) {
        if (link.edge == fromEdge && link.end == fromEnd) continue;
        if (fromSide >= 0 && link.side == fromSide) continue;
        out.push_back({link.edge, arriving ? link.end == 1 : link.end == 0});
    }
    if (warpTable == nullptr) return;
    const std::optional<WarpEndpoint> partner = warpTable->Find(sceneIndex, nodeGuid);
    if (!partner.has_value() || partner->sceneIndex != sceneIndex) return;
    const TrackNode* partnerNode = FindLinkNode(graph, partner->node);
    if (partnerNode == nullptr) return;
    for (const TrackNode::Link& link : partnerNode->links) {
        out.push_back({link.edge, arriving ? link.end == 1 : link.end == 0});
    }
}

TravelDirectionMasks MarkTravelDirection(const TrackGraph& graph, const WarpLinkTable* warpTable,
                                         size_t sceneIndex, const DirectedEdge& start,
                                         const std::unordered_set<Guid, GuidHash>* allowedEdges,
                                         const Guid* stopEdge) {
    TravelDirectionMasks masks;
    if (!graph.edges.contains(start.edge)) return masks;
    if (allowedEdges != nullptr && !allowedEdges->contains(start.edge)) return masks;
    masks[start.edge] = start.along ? kTravelAlongBit : kTravelAgainstBit;
    if (stopEdge != nullptr && start.edge == *stopEdge) return masks;
    std::deque<DirectedEdge> queue{start};
    std::vector<DirectedEdge> neighbors;
    while (!queue.empty()) {
        const DirectedEdge current = queue.front();
        queue.pop_front();
        const TrackEdge& edge = graph.edges.at(current.edge);
        const Guid exitNode = LinkNodeOfEnd(graph, edge, current.along ? 1 : 0);
        const Guid entryNode = LinkNodeOfEnd(graph, edge, current.along ? 0 : 1);
        neighbors.clear();
        CollectNeighbors(graph, warpTable, sceneIndex, exitNode, current.edge,
                         current.along ? 1 : 0, false, neighbors);
        CollectNeighbors(graph, warpTable, sceneIndex, entryNode, current.edge,
                         current.along ? 0 : 1, true, neighbors);
        for (const DirectedEdge& next : neighbors) {
            if (!graph.edges.contains(next.edge)) continue;
            if (allowedEdges != nullptr && !allowedEdges->contains(next.edge)) continue;
            unsigned char& mask = masks[next.edge];
            const unsigned char bit = next.along ? kTravelAlongBit : kTravelAgainstBit;
            if ((mask & bit) != 0) continue;
            mask |= bit;
            if (stopEdge != nullptr && next.edge == *stopEdge) return masks;
            queue.push_back(next);
        }
    }
    return masks;
}

std::optional<int> FollowTravelDirection(const TrackGraph& graph, const Guid& refEdge,
                                         int refDirection, const Guid& targetEdge,
                                         const WarpLinkTable* warpTable, size_t sceneIndex) {
    if (!graph.edges.contains(refEdge) || !graph.edges.contains(targetEdge)) return std::nullopt;
    const bool refAlong = refDirection > 0;
    if (refEdge == targetEdge) return refAlong ? +1 : -1;
    const TravelDirectionMasks masks = MarkTravelDirection(
        graph, warpTable, sceneIndex, DirectedEdge{refEdge, refAlong}, nullptr, &targetEdge);
    const TravelDirectionMasks::const_iterator it = masks.find(targetEdge);
    if (it == masks.end()) return std::nullopt;
    // 打ち切った時点で対象edgeに立っているのは、最初に着いた向きの1ビットだけ。
    return (it->second & kTravelAlongBit) != 0 ? +1 : -1;
}

} // namespace rs::world

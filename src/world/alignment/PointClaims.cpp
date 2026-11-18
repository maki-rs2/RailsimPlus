#include "PointClaims.h"

#include "RailLinkOverlay.h"
#include "TrackEdge.h"
#include "TrackGraph.h"

namespace rs::world {
namespace {

// edgeがnodeにつながるlinkの添字。無ければ-1。
int FindLinkIndex(const TrackNode& node, const Guid& edge) {
    for (size_t index = 0; index < node.links.size(); ++index) {
        if (node.links[index].edge == edge) return static_cast<int>(index);
    }
    return -1;
}

} // namespace

std::vector<PointClaim> CollectPointClaims(
    const std::function<const TrackGraph*(size_t)>& graphForScene, const ForwardScanSpan& span,
    double maxDistanceM) {
    std::vector<PointClaim> claims;
    for (size_t index = 0; index + 1 < span.entries.size(); ++index) {
        const EdgeAheadAcrossScenes& entry = span.entries[index];
        const EdgeAheadAcrossScenes& next = span.entries[index + 1];
        // ワープをまたぐ境目はポイントではない。
        if (entry.warpHops != next.warpHops || entry.sceneIndex != next.sceneIndex) continue;
        if (next.distance > maxDistanceM) break;
        const TrackGraph* graph = graphForScene(entry.sceneIndex);
        if (graph == nullptr) continue;
        const std::unordered_map<Guid, TrackEdge, GuidHash>::const_iterator edgeIt =
            graph->edges.find(entry.edge);
        if (edgeIt == graph->edges.end()) continue;
        const Guid sharedNode = LinkNodeOfEnd(*graph, edgeIt->second, entry.alongEdgeDir ? 1 : 0);
        const TrackNode* nodePtr = FindLinkNode(*graph, sharedNode);
        if (nodePtr == nullptr) continue;
        const TrackNode& node = *nodePtr;
        const int enteredIndex = FindLinkIndex(node, entry.edge);
        if (enteredIndex < 0) continue;
        const int enteredSide = node.links[static_cast<size_t>(enteredIndex)].side;

        PointClaim claim;
        claim.sceneIndex = entry.sceneIndex;
        claim.node = sharedNode;
        claim.distanceM = next.distance;
        if (node.IsBranching(enteredSide)) {
            // 合流で入る: 入ってくる線がそのまま要求になる。
            claim.face = enteredSide;
            claim.slot = node.SlotOfLink(enteredIndex);
            claim.trailing = true;
        } else if (node.IsBranching(1 - enteredSide)) {
            // 分岐で入る: 出口の面で、走査が選んだ線が要求になる。
            const int exitSide = 1 - enteredSide;
            const int exitIndex = FindLinkIndex(node, next.edge);
            if (exitIndex < 0 || node.links[static_cast<size_t>(exitIndex)].side != exitSide) continue;
            claim.face = exitSide;
            claim.slot = node.SlotOfLink(exitIndex);
        } else {
            continue;
        }
        claims.push_back(claim);
    }
    return claims;
}

} // namespace rs::world

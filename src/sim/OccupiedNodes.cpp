#include "OccupiedNodes.h"
#include "../world/alignment/RailLinkOverlay.h"

#include <utility>
#include "../world/alignment/TrackGraph.h"

namespace rs::sim {

NodeSet CollectOccupiedNodes(const std::vector<const TrainOccupiedPath*>& paths,
                             const GraphForScene& graphForScene) {
    NodeSet nodes;
    for (const TrainOccupiedPath* path : paths) {
        if (path == nullptr) continue;
        for (const OccupiedPathEntry& entry : path->entries) {
            const rs::world::TrackGraph* graph = graphForScene(entry.sceneIndex);
            if (graph == nullptr) continue;
            const auto edgeIt = graph->edges.find(entry.edge);
            if (edgeIt == graph->edges.end()) continue;
            const rs::world::TrackEdge& edge = edgeIt->second;
            nodes.insert(edge.nodeA);
            nodes.insert(edge.nodeB);
            nodes.insert(rs::world::LinkNodeOfEnd(*graph, edge, 0));
            nodes.insert(rs::world::LinkNodeOfEnd(*graph, edge, 1));
        }
    }
    return nodes;
}

rs::world::NodeInUseFn MakeNodeInUseFn(NodeSet nodes) {
    return [nodes = std::move(nodes)](const rs::world::Guid& node) {
        return nodes.find(node) != nodes.end();
    };
}

} // namespace rs::sim

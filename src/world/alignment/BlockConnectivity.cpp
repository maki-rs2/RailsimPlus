#include "BlockConnectivity.h"

#include <unordered_set>
#include <utility>

#include "TravelDirectionFollow.h"
#include "SpeedSignal.h"
#include "TrackEdge.h"
#include "TrackGraph.h"

namespace rs::world {
namespace {

// edgeの端の先に続くedgeがあり、そのどれも閉塞に含まれないなら途切れ目。
bool IsCutEnd(const TrackGraph& graph, const WarpLinkTable* warpTable, size_t sceneIndex,
              const std::unordered_set<Guid, GuidHash>& blockEdges, const Guid& edgeGuid,
              const TrackEdge& edge, int end) {
    std::vector<DirectedEdge> neighbors;
    CollectNeighbors(graph, warpTable, sceneIndex, EdgeLinkNode(edge, end), edgeGuid, end, true,
                     neighbors);
    if (neighbors.empty()) return false;
    for (const DirectedEdge& next : neighbors) {
        if (blockEdges.contains(next.edge)) return false;
    }
    return true;
}

} // namespace

BlockConnectivity AnalyzeBlockConnectivity(const TrackGraph& graph, const Block& block,
                                           const WarpLinkTable* warpTable, size_t sceneIndex) {
    BlockConnectivity result;
    result.block = block.guid;
    std::unordered_set<Guid, GuidHash> blockEdges;
    for (const Guid& edge : block.edges) {
        if (graph.edges.contains(edge)) blockEdges.insert(edge);
    }
    std::unordered_set<Guid, GuidHash> assigned;
    for (const Guid& seed : block.edges) {
        if (!blockEdges.contains(seed) || assigned.contains(seed)) continue;
        // どちらの向きでも手前と先の両方をたどるので片方でも同じ成分に届くが、
        // 向きごとの行き止まりに左右されないよう両方の和を取る。
        std::unordered_set<Guid, GuidHash> reached;
        for (const bool along : {true, false}) {
            for (const auto& entry :
                 MarkTravelDirection(graph, warpTable, sceneIndex, DirectedEdge{seed, along},
                                     &blockEdges, nullptr)) {
                reached.insert(entry.first);
            }
        }
        BlockComponent component;
        for (const Guid& edge : block.edges) {
            if (reached.contains(edge) && assigned.insert(edge).second) {
                component.edges.push_back(edge);
            }
        }
        result.components.push_back(std::move(component));
    }
    if (result.components.size() < 2) return result;
    for (BlockComponent& component : result.components) {
        for (const Guid& edgeGuid : component.edges) {
            const TrackEdge& edge = graph.edges.at(edgeGuid);
            for (const int end : {0, 1}) {
                if (IsCutEnd(graph, warpTable, sceneIndex, blockEdges, edgeGuid, edge, end)) {
                    component.cutPoints.push_back({edgeGuid, end});
                }
            }
        }
    }
    return result;
}

} // namespace rs::world

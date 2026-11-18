#include "TrainOccupiedPath.h"

#include <algorithm>

#include "../world/alignment/RailLinkOverlay.h"
#include "../world/alignment/TrackGraph.h"
namespace rs::sim {
namespace {
bool IsConnected(const rs::world::TrackGraph& firstGraph, const rs::world::TrackEdge& first,
                 int firstDir, const rs::world::TrackGraph& secondGraph,
                 const rs::world::TrackEdge& second, int secondDir) {
    const rs::world::Guid firstEnd = rs::world::LinkNodeOfEnd(firstGraph, first, firstDir >= 0 ? 1 : 0);
    const rs::world::Guid secondStart =
        rs::world::LinkNodeOfEnd(secondGraph, second, -secondDir >= 0 ? 1 : 0);
    return firstEnd == secondStart;
}
} // namespace

bool ValidateOccupiedPath(const TrainOccupiedPath& path, const GraphForScene& graphForScene) {
    if (path.entries.empty() || path.entries.front().edge != path.head.edge) {
        return false;
    }
    for (size_t index = 0; index < path.entries.size(); ++index) {
        const OccupiedPathEntry& entry = path.entries[index];
        const rs::world::TrackGraph* graph = graphForScene(entry.sceneIndex);
        if (graph == nullptr || !graph->edges.contains(entry.edge)) return false;
        if (index == 0) continue;
        const OccupiedPathEntry& previous = path.entries[index - 1];
        if (previous.sceneIndex != entry.sceneIndex || entry.warpToPrevious) continue;
        const rs::world::TrackGraph* previousGraph = graphForScene(previous.sceneIndex);
        if (previousGraph == nullptr) return false;
        const rs::world::TrackEdge& previousEdge = previousGraph->edges.at(previous.edge);
        const rs::world::TrackEdge& edge = graph->edges.at(entry.edge);
        if (!IsConnected(*previousGraph, previousEdge, previous.direction, *graph, edge, entry.direction)) return false;
    }
    return true;
}

bool ChaseBackwardOnPath(const TrainOccupiedPath& path, const GraphForScene& graphForScene,
                         double distanceM, size_t& outSceneIndex, rs::world::TrackPosition& outPos) {
    if (!ValidateOccupiedPath(path, graphForScene) || distanceM < 0.0) return false;
    double remaining = distanceM;
    for (size_t index = 0; index < path.entries.size(); ++index) {
        const OccupiedPathEntry& entry = path.entries[index];
        const rs::world::TrackEdge& edge = graphForScene(entry.sceneIndex)->edges.at(entry.edge);
        const double length = rs::world::EdgeLength(edge);
        const double startS = index == 0 ? path.head.s : (entry.direction >= 0 ? 0.0 : length);
        const double available = entry.direction >= 0 ? length - startS : startS;
        if (remaining <= available) {
            outSceneIndex = entry.sceneIndex;
            outPos.edge = entry.edge;
            outPos.s = startS + (entry.direction >= 0 ? remaining : -remaining);
            outPos.direction = -entry.direction;
            return true;
        }
        remaining -= available;
    }
    return false;
}

std::vector<rs::world::OccupiedRangeAcrossScenes> OccupiedRangesFromPath(
    const TrainOccupiedPath& path, const GraphForScene& graphForScene, double lengthM) {
    constexpr double kCoverageToleranceM = 1e-6;
    if (lengthM <= 0.0 || !ValidateOccupiedPath(path, graphForScene)) {
        return {};
    }

    std::vector<rs::world::OccupiedRangeAcrossScenes> ranges;
    double remaining = lengthM;
    for (size_t index = 0; index < path.entries.size() && remaining > 0.0; ++index) {
        const OccupiedPathEntry& entry = path.entries[index];
        if (entry.direction != +1 && entry.direction != -1) {
            return {};
        }
        const rs::world::TrackGraph* graph = graphForScene(entry.sceneIndex);
        if (graph == nullptr) {
            return {};
        }
        const rs::world::TrackEdge& edge = graph->edges.at(entry.edge);
        const double edgeLengthM = rs::world::EdgeLength(edge);
        const double startS = index == 0 ? path.head.s : (entry.direction > 0 ? 0.0 : edgeLengthM);
        const double availableM = entry.direction > 0 ? edgeLengthM - startS : startS;
        if (availableM < 0.0) {
            return {};
        }

        const double consumedM = std::min(remaining, availableM);
        if (consumedM > 0.0) {
            const double endS = startS + (entry.direction > 0 ? consumedM : -consumedM);
            ranges.push_back({entry.sceneIndex, entry.edge, std::min(startS, endS),
                              std::max(startS, endS)});
        }
        remaining -= consumedM;
    }
    if (remaining > kCoverageToleranceM) {
        return {};
    }
    return ranges;
}

void TrimOccupiedPath(TrainOccupiedPath& path, const GraphForScene& graphForScene,
                      double keepLengthM) {
    if (path.entries.empty()) return;
    const double keep = std::max(0.0, keepLengthM);
    double covered = 0.0;
    size_t count = 0;
    for (; count < path.entries.size(); ++count) {
        const OccupiedPathEntry& entry = path.entries[count];
        const rs::world::TrackGraph* graph = graphForScene(entry.sceneIndex);
        if (graph == nullptr || !graph->edges.contains(entry.edge)) break;
        const double length = rs::world::EdgeLength(graph->edges.at(entry.edge));
        const double available = count == 0
            ? (entry.direction >= 0 ? length - path.head.s : path.head.s) : length;
        covered += std::max(0.0, available);
        if (covered >= keep) {
            ++count;
            break;
        }
    }
    path.entries.resize(count);
}

void ExtendOccupiedPathAtTail(TrainOccupiedPath& path, const GraphForScene& graphForScene,
                              const rs::world::WarpLinkTable& warpTable, double keepLengthM) {
    if (path.entries.empty() || keepLengthM <= 0.0) return;

    double covered = 0.0;
    for (size_t index = 0; index < path.entries.size(); ++index) {
        const OccupiedPathEntry& entry = path.entries[index];
        const rs::world::TrackGraph* graph = graphForScene(entry.sceneIndex);
        if (graph == nullptr || !graph->edges.contains(entry.edge)) return;
        const double length = rs::world::EdgeLength(graph->edges.at(entry.edge));
        const double available = index == 0
            ? (entry.direction >= 0 ? length - path.head.s : path.head.s) : length;
        covered += std::max(0.0, available);
    }
    if (covered >= keepLengthM) return;

    const OccupiedPathEntry& back = path.entries.back();
    const rs::world::TrackGraph* graph = graphForScene(back.sceneIndex);
    if (graph == nullptr || !graph->edges.contains(back.edge)) return;
    const rs::world::TrackEdge& edge = graph->edges.at(back.edge);
    rs::world::TrackPosition tip;
    tip.edge = back.edge;
    tip.s = back.direction >= 0 ? rs::world::EdgeLength(edge) : 0.0;
    tip.direction = back.direction;

    const rs::world::WarpAdvanceResult result = rs::world::AdvanceAcrossScenes(
        graphForScene, warpTable, back.sceneIndex, tip, keepLengthM - covered);
    if (result.visitedEdges.empty()) return;
    const rs::world::WarpVisitedEdge& first = result.visitedEdges.front();
    if (first.sceneIndex != back.sceneIndex || first.edge.edge != tip.edge) return;

    for (size_t index = 1; index < result.visitedEdges.size(); ++index) {
        const rs::world::WarpVisitedEdge& visited = result.visitedEdges[index];
        path.entries.push_back(OccupiedPathEntry{visited.sceneIndex, visited.edge.edge,
                                                 visited.edge.direction, visited.viaWarp});
    }
}

} // namespace rs::sim

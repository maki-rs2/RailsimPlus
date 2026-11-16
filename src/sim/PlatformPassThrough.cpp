#include "PlatformPassThrough.h"

#include <algorithm>
#include <limits>

namespace rs::sim {
namespace {

bool IsRangeOnPlatform(const GraphForScene& graphForScene,
                       const rs::world::OccupiedRangeAcrossScenes& range,
                       const rs::world::Guid& platformId) {
    const rs::world::TrackGraph* graph = graphForScene(range.sceneIndex);
    if (graph == nullptr) return false;
    const std::unordered_map<rs::world::Guid, rs::world::Guid, rs::world::GuidHash>::const_iterator
        platformIt = graph->edgeToPlatform.find(range.edge);
    return platformIt != graph->edgeToPlatform.end() && platformIt->second == platformId;
}

} // namespace

bool IsPlatformOccupied(const GraphForScene& graphForScene,
                        const rs::world::WarpLinkTable& warpTable, size_t sceneIndex,
                        const rs::world::TrackPosition& head,
                        const TrainOccupiedPath& occupiedPath, double trainLengthM,
                        const rs::world::Guid& platformId) {
    if (rs::world::GuidIsNil(platformId)) return false;
    const rs::world::TrackGraph* headGraph = graphForScene(sceneIndex);
    if (headGraph != nullptr) {
        const std::unordered_map<rs::world::Guid, rs::world::Guid, rs::world::GuidHash>::const_iterator
            headPlatformIt = headGraph->edgeToPlatform.find(head.edge);
        if (headPlatformIt != headGraph->edgeToPlatform.end() &&
            headPlatformIt->second == platformId) return true;
    }

    rs::world::TrackPosition bodyStart = head;
    bodyStart.direction = -bodyStart.direction;
    std::vector<rs::world::OccupiedRangeAcrossScenes> occupied =
        OccupiedRangesFromPath(occupiedPath, graphForScene, trainLengthM);
    if (occupied.empty()) {
        occupied = rs::world::ScanOccupiedRangesAheadAcrossScenes(
            graphForScene, warpTable, sceneIndex, bodyStart, trainLengthM);
    }
    for (const rs::world::OccupiedRangeAcrossScenes& range : occupied) {
        if (IsRangeOnPlatform(graphForScene, range, platformId)) return true;
    }
    return false;
}

rs::world::Guid FindStoppablePlatformUnderTrain(const GraphForScene& graphForScene,
                                                const rs::world::WarpLinkTable& warpTable,
                                                size_t sceneIndex,
                                                const rs::world::TrackPosition& head,
                                                double trainLengthM) {
    const rs::world::TrackGraph* graph = graphForScene(sceneIndex);
    if (graph == nullptr) return rs::world::NilGuid();

    // 編成の中央の世界位置。番線の目印の中央とどれだけ離れているかで、複数当たった場合に選ぶ。
    rs::world::TrackPosition backward = head;
    backward.direction = -backward.direction;
    const rs::world::TrackPosition center =
        rs::world::Advance(*graph, backward, trainLengthM * 0.5).pos;
    const std::unordered_map<rs::world::Guid, rs::world::TrackEdge, rs::world::GuidHash>::const_iterator
        centerEdgeIt = graph->edges.find(center.edge);
    if (centerEdgeIt == graph->edges.end()) return rs::world::NilGuid();
    const rs::core::Vec3 centerPos =
        rs::world::EvaluateAt(centerEdgeIt->second, center.s, center.direction).pos;

    const TrainOccupiedPath noPath;
    rs::world::Guid best = rs::world::NilGuid();
    float bestDistance = 0.0f;
    for (const rs::world::PlatformStopInfo& platform : graph->platforms) {
        if (!platform.stoppable) continue;
        if (!IsPlatformOccupied(graphForScene, warpTable, sceneIndex, head, noPath, trainLengthM,
                                platform.id)) {
            continue;
        }
        float distance = std::numeric_limits<float>::max();
        for (const rs::world::Guid& edgeGuid : platform.railEdges) {
            const std::unordered_map<rs::world::Guid, rs::world::TrackEdge, rs::world::GuidHash>::const_iterator
                edgeIt = graph->edges.find(edgeGuid);
            if (edgeIt == graph->edges.end()) continue;
            const double middle = rs::world::EdgeLength(edgeIt->second) * 0.5;
            const rs::core::Vec3 markerPos = rs::world::EvaluateAt(edgeIt->second, middle, +1).pos;
            distance = std::min(distance, (markerPos - centerPos).Length());
        }
        if (rs::world::GuidIsNil(best) || distance < bestDistance) {
            best = platform.id;
            bestDistance = distance;
        }
    }
    return best;
}

void UpdatePassingPlatformLatch(const GraphForScene& graphForScene,
                                const rs::world::WarpLinkTable& warpTable, size_t sceneIndex,
                                const rs::world::TrackPosition& head,
                                const TrainOccupiedPath& occupiedPath, double trainLengthM,
                                rs::world::Guid& passingPlatform, bool& passingEntered) {
    if (rs::world::GuidIsNil(passingPlatform)) return;
    const bool occupied = IsPlatformOccupied(graphForScene, warpTable, sceneIndex, head,
                                             occupiedPath, trainLengthM, passingPlatform);
    if (occupied) {
        passingEntered = true;
    } else if (passingEntered) {
        passingPlatform = rs::world::NilGuid();
        passingEntered = false;
    }
}

bool BeginPlatformPassThrough(
    const rs::world::PlatformRoutineElement* routineElement,
    const rs::world::PlatformStopInfo& platform, rs::world::Guid& passingPlatform,
    bool& passingEntered,
    const std::function<void(const rs::world::PlatformStopInfo&)>& consumeRoutine) {
    if (routineElement == nullptr || routineElement->action != rs::world::DiaAction::Pass) return false;
    passingPlatform = platform.id;
    passingEntered = false;
    if (consumeRoutine) consumeRoutine(platform);
    return true;
}

} // namespace rs::sim

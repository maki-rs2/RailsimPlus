#include "SceneSignalPhase.h"
#include "../../world/alignment/TrackGraph.h"

namespace rs::app {
SignalPhaseOutputs RunSignalBlockPhase(
    const std::function<rs::world::TrackGraph*(size_t)>& graphForScene, size_t sceneCount,
    const std::vector<rs::sim::OccupancyTrain>& occupancyTrains,
    rs::sim::BlockOccupancyTracker& blockOccupancyTracker) {
    rs::sim::BlockOccupancySnapshot mergedOccupancy;
    for (size_t sceneIndex = 0; sceneIndex < sceneCount; ++sceneIndex) {
        rs::world::TrackGraph* graph = graphForScene(sceneIndex);
        if (graph == nullptr) break;
        rs::sim::BlockOccupancySnapshot sceneOccupancy =
            rs::sim::ComputeBlockOccupancy(*graph, occupancyTrains, sceneIndex);
        mergedOccupancy.insert(sceneOccupancy.begin(), sceneOccupancy.end());
    }
    blockOccupancyTracker.Update(std::move(mergedOccupancy));
    SignalPhaseOutputs outputs;
    for (const auto& [blockGuid, occupancy] : blockOccupancyTracker.Previous()) {
        if (!occupancy.occupantTrainIds.empty()) outputs.previousOccupiedBlockGuids.insert(blockGuid);
    }
    return outputs;
}

void RecomputeSignalAspects(const std::function<rs::world::TrackGraph*(size_t)>& graphForScene,
                            size_t sceneCount, const rs::world::WarpLinkTable& warpTable,
                            const rs::sim::BlockOccupancyTracker& blockOccupancyTracker,
                            rs::sim::SignalAspectRuntime& signalAspectRuntime) {
    const std::function<const rs::world::TrackGraph*(size_t)> constGraphForScene =
        [&graphForScene](size_t sceneIndex) -> const rs::world::TrackGraph* {
        return graphForScene(sceneIndex);
    };
    signalAspectRuntime.Recompute(constGraphForScene, sceneCount, warpTable,
                                  blockOccupancyTracker.Current(), rs::sim::SignalStageSpeeds);
}
} // namespace rs::app

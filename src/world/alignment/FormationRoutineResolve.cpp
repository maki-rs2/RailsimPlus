#include "FormationRoutineResolve.h"

#include <algorithm>

#include "TrackGraph.h"
#include "PlatformRegistry.h"
#include "../scene/TrainTagRegistry.h"

namespace rs::world {

int MigrateTrainRoutinesToFormations(TrackGraph& graph, const TrainTagRegistry& tagRegistry) {
    int migratedCount = 0;
    for (auto& [nodeGuid, node] : graph.nodes) {
        (void)nodeGuid;
        for (const PointTrainRoutine& trainRoutine : node.pointTrainRoutines) {
            const Guid formationId = tagRegistry.FormationOfTrain(trainRoutine.trainId);
            if (GuidIsNil(formationId)) continue;
            const bool alreadyPresent = std::any_of(
                node.pointFormationRoutines.begin(), node.pointFormationRoutines.end(),
                [&formationId](const PointFormationRoutine& item) {
                    return item.formationId == formationId;
                });
            if (alreadyPresent) continue;
            node.pointFormationRoutines.push_back({formationId, trainRoutine.routine});
            ++migratedCount;
        }
        node.pointTrainRoutines.clear();
        for (const PointTrainCondition& trainCondition : node.pointTrainConditions) {
            const Guid formationId = tagRegistry.FormationOfTrain(trainCondition.trainId);
            if (GuidIsNil(formationId)) continue;
            const bool alreadyPresent = std::any_of(
                node.pointFormationConditions.begin(), node.pointFormationConditions.end(),
                [&formationId](const PointFormationCondition& item) {
                    return item.formationId == formationId;
                });
            if (alreadyPresent) continue;
            node.pointFormationConditions.push_back({formationId, trainCondition.conditions});
            ++migratedCount;
        }
        node.pointTrainConditions.clear();
    }
    for (PlatformStopInfo& platform : graph.platforms) {
        for (const PlatformTrainRoutine& trainRoutine : platform.trainRoutines) {
            const Guid formationId = tagRegistry.FormationOfTrain(trainRoutine.trainId);
            if (GuidIsNil(formationId)) continue;
            const bool alreadyPresent = std::any_of(
                platform.formationRoutines.begin(), platform.formationRoutines.end(),
                [&formationId](const PlatformFormationRoutine& item) {
                    return item.formationId == formationId;
                });
            if (alreadyPresent) continue;
            platform.formationRoutines.push_back({formationId, trainRoutine.routine});
            ++migratedCount;
        }
        platform.trainRoutines.clear();
    }
    return migratedCount;
}

int MigrateTrainRoutinesToFormations(
    const std::function<TrackGraph*(size_t)>& graphForScene, size_t sceneCount,
    const TrainTagRegistry& tagRegistry) {
    int migratedCount = 0;
    for (size_t sceneIndex = 0; sceneIndex < sceneCount; ++sceneIndex) {
        TrackGraph* graph = graphForScene ? graphForScene(sceneIndex) : nullptr;
        if (graph != nullptr) migratedCount += MigrateTrainRoutinesToFormations(*graph, tagRegistry);
    }
    return migratedCount;
}

void RegisterPlacedTrainFormation(const Guid& formationId, TrainId trainId,
                                  TrainTagRegistry& tagRegistry) {
    tagRegistry.LinkTrainToFormation(trainId, formationId);
}

}  // namespace rs::world

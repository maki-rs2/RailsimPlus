//
// TrainPlacement.cpp
//
#include "TrainPlacement.h"

#include "../../plugins/load/TrainAssembly.h"
#include "../../sim/TrainVehicle.h"
#include "../../world/scene/ScenePlacement.h"
#include "../../world/scene/SceneEntity.h"
#include "../../world/scene/SceneRegistry.h"
#include "../../world/alignment/TrackGraph.h"

namespace rs::app {
std::vector<rs::world::TrackPlacement> SolveCarPlacements(const std::vector<FormationCar>& cars,
                                                          const rs::world::TrackPosition& head,
                                                          const rs::world::TrackGraph& graph) {
    if (cars.empty()) return {};

    std::vector<rs::legacy_plugin::CarSpec> specs;
    specs.reserve(cars.size());
    for (const FormationCar& car : cars) {
        rs::legacy_plugin::CarSpec spec;
        spec.frontLimit = car.frontLimit;
        spec.tailLimit = car.tailLimit;
        spec.reversed = car.reversed;
        specs.push_back(spec);
    }
    const std::vector<rs::world::TrackPosition> heads =
        rs::legacy_plugin::SolveTrainHeads(graph, head, specs);

    std::vector<rs::world::TrackPlacement> placements;
    placements.reserve(cars.size());
    for (size_t i = 0; i < cars.size(); ++i) {
        rs::world::TrackPlacement placement;
        placement.position = heads[i];
        placement.yawOffsetDeg = rs::world::CarYawOffsetDeg(cars[i].reversed);
        placements.push_back(placement);
    }
    return placements;
}

bool CarsFitOnTrack(const std::vector<FormationCar>& cars, const rs::world::TrackPosition& head,
                    const rs::world::TrackGraph& graph) {
    double totalLengthM = 0.0;
    for (const FormationCar& car : cars) totalLengthM += car.frontLimit - car.tailLimit;
    // atEndは「辿りきれず行き止まりで止まった」ときだけ立つ(ちょうど終端で収まるなら立たない)。
    return !rs::sim::ChaseBackward(graph, head, totalLengthM).atEnd;
}

std::vector<rs::world::SceneEntity> BuildTrainRepeating(const std::vector<FormationCar>& cars,
                                                           const rs::world::TrackPosition& head,
                                                           const rs::world::TrackGraph& graph,
                                                           size_t sceneIndex, TrainId* groupIdOut) {
    // 読み込めなかった車両は寸法を持たず実体にならないので、置く車両から外す
    // (長さ0なので、残りの車両の位置は外しても変わらない)。
    std::vector<FormationCar> entries;
    entries.reserve(cars.size());
    for (const FormationCar& car : cars) {
        if (!car.unresolved) entries.push_back(car);
    }
    if (entries.empty()) return {};

    const std::vector<rs::world::TrackPlacement> placements = SolveCarPlacements(entries, head, graph);

    // このプロセス内で一意なグルーピングIDを発番する(複数編成の重ね配置を区別するため)。
    const TrainId trainId = rs::world::GenerateGuid();
    if (groupIdOut) *groupIdOut = trainId;

    std::vector<rs::world::SceneEntity> entities;
    entities.reserve(entries.size());
    for (size_t i = 0; i < entries.size(); ++i) {
        const FormationCar& car = entries[i];

        rs::world::SceneEntity entity;
        entity.folderUtf8 = car.folderUtf8;
        entity.category = rs::world::SceneEntityCategory::Car;
        entity.placement = placements[i];
        entity.reversed = car.reversed;
        entity.switchOptions = car.switchOptions;
        entity.trainId = trainId;
        entity.carOrder = static_cast<int>(i);
        entity.placementSceneIndex = sceneIndex;
        entities.push_back(std::move(entity));
    }
    return entities;
}

int PlaceTrainRepeating(rs::world::SceneRegistry& registry, const std::vector<FormationCar>& cars,
                          const rs::world::TrackPosition& head,
                          const rs::world::TrackGraph& graph, size_t sceneIndex, TrainId* groupIdOut) {
    std::vector<rs::world::SceneEntity> entities =
        BuildTrainRepeating(cars, head, graph, sceneIndex, groupIdOut);
    for (rs::world::SceneEntity& entity : entities) registry.AddEntity(entity);
    return static_cast<int>(entities.size());
}

} // namespace rs::app

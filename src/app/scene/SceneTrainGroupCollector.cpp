#include "SceneTrainGroupCollector.h"

#include <algorithm>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <spdlog/spdlog.h>

#include "../../sim/SimTuning.h"
#include "../../world/alignment/GuidJson.h"

namespace rs::app {

namespace {

// entityId(スロットのentityIdと同じ形式のGuid)一致で該当スロットindexを
// 引くための補助マップ。slots配列をentityごとに何度も線形探索しないための
// 一時インデックス(SceneSlotPool::FindSlotByEntityIdは同等の走査をentity単位で
// 行うため、複数entity分をまとめて処理するここではO(1)化する)。
using EntityIdSlotMap = std::unordered_map<rs::world::Guid, size_t, rs::world::GuidHash>;

EntityIdSlotMap BuildEntityIdToSlotIndex(const std::vector<SceneSlotPool::Slot>& slots) {
    EntityIdSlotMap result;
    for (size_t index = 0; index < slots.size(); ++index) {
        if (!slots[index].used || !slots[index].isTrainCar) continue;
        result[slots[index].entityId] = index;
    }
    return result;
}

} // namespace

double MaxAxleChaseOffset(const rs::legacy_plugin::TrainVehicleTemplate& tmpl, bool reversed) {
    double maxOffset = tmpl.vehicle.frontLimit - tmpl.vehicle.tailLimit;
    rs::sim::TrainVehicle oriented;
    oriented.frontLimit = tmpl.vehicle.frontLimit;
    oriented.tailLimit = tmpl.vehicle.tailLimit;
    oriented.flipped = reversed;
    for (const rs::sim::Axle& axle : tmpl.vehicle.axles) {
        maxOffset = std::max(maxOffset, rs::sim::AxleChaseOffset(oriented, axle));
    }
    return maxOffset;
}

std::vector<TrainGroupSnapshot> CollectTrainGroups(
    const rs::world::SceneRegistry& registry, const std::vector<SceneSlotPool::Slot>& slots,
    const SceneSlotPool& slotPool, size_t activeSceneIndex,
    std::unordered_set<rs::world::Guid, rs::world::GuidHash>* warnedEntityIds) {
    const EntityIdSlotMap entityIdToSlotIndex =BuildEntityIdToSlotIndex(slots);

    // GPU常駐スロットの有無に関わらず、registry上のCarエンティティ全件をtrainIdで
    // グルーピングする。
    std::map<TrainId, std::vector<const rs::world::SceneEntity*>> groupedEntities;
    for (const rs::world::SceneEntity& entity : registry.Entities()) {
        if (entity.category != rs::world::SceneEntityCategory::Car) continue;
        groupedEntities[entity.trainId].push_back(&entity);
    }

    std::vector<TrainGroupSnapshot> groups;
    groups.reserve(groupedEntities.size());
    for (auto& [trainId, entities] : groupedEntities) {
        if (entities.empty()) continue;
        std::sort(entities.begin(), entities.end(),
                  [](const rs::world::SceneEntity* left, const rs::world::SceneEntity* right) {
                      return left->carOrder < right->carOrder;
                  });

        TrainGroupSnapshot group;
        group.trainId = trainId;
        group.leadEntity = entities.front();
        if (group.leadEntity != nullptr) {
            group.leadPlacement =
                std::get_if<rs::world::TrackPlacement>(&group.leadEntity->placement);
        }

        for (const rs::world::SceneEntity* entity : entities) {
            const EntityIdSlotMap::const_iterator slotIt =
                entityIdToSlotIndex.find(entity->guid);
            const rs::legacy_plugin::TrainVehicleTemplate* trainTemplate = nullptr;
            if (slotIt != entityIdToSlotIndex.end()) {
                group.slotIndices.push_back(slotIt->second);
                trainTemplate = &slots[slotIt->second].carSnapshot.trainTemplate;
            } else {
                trainTemplate = slotPool.FindCachedCarTemplate(entity->guid);
                if (trainTemplate == nullptr) {
                    group.hasUnresolvedCars = true;
                    // 非常駐車両は毎tick現れるので、所有者がレイアウトごとに警告を抑制する。
                    if (entity->placementSceneIndex != activeSceneIndex && warnedEntityIds != nullptr &&
                        warnedEntityIds->insert(entity->guid).second) {
                        spdlog::warn(
                            "[scene] 編成性能の積算対象から除外: GPU常駐スロットも過去の"
                            "ロード成功キャッシュも無い車両エンティティ({})",
                            rs::world::GuidToHex(entity->guid));
                    }
                    continue;
                }
            }

            group.trainLengthM +=
                trainTemplate->vehicle.frontLimit - trainTemplate->vehicle.tailLimit;
            group.occupiedPathKeepLengthM +=
                MaxAxleChaseOffset(*trainTemplate, entity->reversed);
            group.maxVelocityKmh =
                std::max(group.maxVelocityKmh, double(trainTemplate->vehicle.maxVelocity));
            group.maxAccelerationKmhPerS = std::max(
                group.maxAccelerationKmhPerS, double(trainTemplate->vehicle.maxAcceleration));
            group.maxDecelerationKmhPerS = std::max(
                group.maxDecelerationKmhPerS, double(trainTemplate->vehicle.maxDeceleration));
            group.doorClosingTimeSeconds =
                std::max(group.doorClosingTimeSeconds, trainTemplate->vehicle.doorClosingTime);
        }
        group.maxDecelerationKmhPerS =
            std::max(group.maxDecelerationKmhPerS,
                     rs::sim::SimTuning::Embedded().seekAhead.minGroupDecelerationKmhPerS);
        groups.push_back(std::move(group));
    }
    return groups;
}

} // namespace rs::app

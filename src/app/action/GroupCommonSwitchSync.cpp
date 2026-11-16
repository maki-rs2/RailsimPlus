#include "GroupCommonSwitchSync.h"

#include "../../world/scene/SceneEntity.h"
#include "../../world/scene/SceneRegistry.h"

namespace rs::app {

void PropagateGroupCommonSwitch(rs::world::SceneRegistry& registry,
                                const SwitchDefsLookup& switchDefsLookup,
                                const rs::world::Guid& sourceEntityId, std::size_t sourceIndex,
                                std::int32_t value) {
    const rs::world::SceneEntity* source = registry.FindEntity(sourceEntityId);
    if (source == nullptr || source->category != rs::world::SceneEntityCategory::Car) return;

    const std::vector<rs::legacy_pi::SwitchDef>* sourceDefs = switchDefsLookup(source->folderUtf8);
    if (sourceDefs == nullptr || sourceIndex >= sourceDefs->size()) return;
    const std::string groupName = (*sourceDefs)[sourceIndex].groupCommon;
    if (groupName.empty()) return;

    const rs::world::TrainId trainId = source->trainId;

    // 反映先を先に確定させてからSetSwitchOptionを呼ぶ(走査中のvector参照を
    // 書き換えの副作用から独立させるため)。
    std::vector<rs::world::Guid> carIds;
    for (const rs::world::SceneEntity& entity : registry.Entities()) {
        if (entity.category == rs::world::SceneEntityCategory::Car && entity.trainId == trainId) {
            carIds.push_back(entity.guid);
        }
    }

    for (const rs::world::Guid& carId : carIds) {
        const rs::world::SceneEntity* car = registry.FindEntity(carId);
        if (car == nullptr) continue;
        const std::vector<rs::legacy_pi::SwitchDef>* defs = switchDefsLookup(car->folderUtf8);
        if (defs == nullptr) continue;
        for (std::size_t i = 0; i < defs->size(); ++i) {
            if ((*defs)[i].groupCommon == groupName) {
                registry.SetSwitchOption(carId, i, value);
            }
        }
    }
}

} // namespace rs::app

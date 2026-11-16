// SceneryTransformValues.h - 選択中の情景の座標欄へ出す値と混在状態
#pragma once

#include <array>
#include <cmath>
#include <span>

#include "../../world/scene/SceneEntity.h"
#include "../../world/scene/ScenePlacement.h"

namespace rs::app {

struct SceneryTransformValues {
    std::array<float, 6> values{}; // x, y, z, yaw, pitch, roll
    std::array<bool, 6> mixed{};
    bool hasValue = false;
};

inline SceneryTransformValues CollectSceneryTransformValues(std::span<const rs::world::SceneEntity> entities) {
    SceneryTransformValues result;
    for (const rs::world::SceneEntity& entity : entities) {
        if (entity.category != rs::world::SceneEntityCategory::Scenery) continue;
        const rs::world::FreePlacement* placement = std::get_if<rs::world::FreePlacement>(&entity.placement);
        if (placement == nullptr) continue;
        const std::array<float, 6> current{placement->position.x, placement->position.y, placement->position.z,
                                           placement->yawDeg, placement->pitchDeg, placement->rollDeg};
        if (!result.hasValue) {
            result.values = current;
            result.hasValue = true;
        } else {
            for (size_t i = 0; i < current.size(); ++i) {
                if (std::abs(result.values[i] - current[i]) > 0.0001f) result.mixed[i] = true;
            }
        }
    }
    return result;
}

} // namespace rs::app

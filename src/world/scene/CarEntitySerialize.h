#pragma once

#include <nlohmann/json.hpp>

#include "SceneEntity.h"

namespace rs::world {

nlohmann::json CarEntityToJson(const SceneEntity& entity);
SceneEntity CarEntityFromJson(const nlohmann::json& json);

}  // namespace rs::world

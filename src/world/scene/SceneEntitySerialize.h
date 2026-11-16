#pragma once

#include <nlohmann/json.hpp>

#include "SceneEntity.h"

namespace rs::world {

nlohmann::json SceneEntityToJson(const SceneEntity& entity);
SceneEntity SceneEntityFromJson(const nlohmann::json& json);

}  // namespace rs::world

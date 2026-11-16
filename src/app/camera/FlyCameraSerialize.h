#pragma once

#include <nlohmann/json.hpp>

#include "FlyCamera.h"

namespace rs::app {

nlohmann::json FlyCameraToJson(const FlyCamera& camera);
FlyCamera FlyCameraFromJson(const nlohmann::json& json);

}  // namespace rs::app

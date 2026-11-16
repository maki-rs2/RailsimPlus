#pragma once

#include <nlohmann/json.hpp>

#include "TrackGraph.h"

namespace rs::world {

nlohmann::json PlatformStopInfoToJson(const PlatformStopInfo& platform);
PlatformStopInfo PlatformStopInfoFromJson(const nlohmann::json& json);

}  // namespace rs::world

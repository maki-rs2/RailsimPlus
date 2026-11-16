#pragma once

#include <nlohmann/json.hpp>

#include "GameClock.h"

namespace rs::world {

nlohmann::json GameClockToJson(const GameClock& clock);
void ApplyGameClockFromJson(const nlohmann::json& json, GameClock& clock);

}  // namespace rs::world

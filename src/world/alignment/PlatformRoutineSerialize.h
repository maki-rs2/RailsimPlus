#pragma once

#include <vector>

#include <nlohmann/json.hpp>

#include "PlatformRoutine.h"

namespace rs::world {

// 番線の停車ルーチン(要素列)のJSON変換。編成別・タグ別ルーチンとの共用。
nlohmann::json PlatformRoutineToJson(const std::vector<PlatformRoutineElement>& routine);
std::vector<PlatformRoutineElement> PlatformRoutineFromJson(const nlohmann::json& json);

}  // namespace rs::world

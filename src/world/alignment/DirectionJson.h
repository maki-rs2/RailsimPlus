//
// DirectionJson.h - 軌道上の進行向き(TrackPosition::direction 等の ±1)と
// .rsl の "forward"/"backward" 表記の相互変換。配置物・配置車両・走行状態・信号が
// 同じ表記を使うため、変換を1箇所へ集約する。
//
#pragma once

#include <string>

#include <nlohmann/json.hpp>

namespace rs::world {

inline const char* DirectionToJson(int direction) {
    return direction > 0 ? "forward" : "backward";
}

inline int DirectionFromJson(const nlohmann::json& json, int fallback = 1) {
    if (!json.is_string()) return fallback;
    return json.get<std::string>() == "backward" ? -1 : 1;
}

}  // namespace rs::world

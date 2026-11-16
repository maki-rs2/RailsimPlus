#pragma once

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "../../core/Math.h"

namespace rs::world {

template <class T>
T JsonReadValue(const nlohmann::json& json, const char* key, const T& fallback, const char* logTag) {
    try {
        if (json.contains(key)) {
            return json.at(key).get<T>();
        }
    } catch (const nlohmann::json::exception&) {
        spdlog::warn("[{}] フィールド '{}' が不正なため既定値を使用します", logTag, key);
    }
    return fallback;
}

inline nlohmann::json Vec3ToJson(const rs::core::Vec3& value) {
    return nlohmann::json::array({ value.x, value.y, value.z });
}

inline rs::core::Vec3 Vec3FromJson(const nlohmann::json& json, const rs::core::Vec3& fallback,
                                   const char* logTag) {
    if (!json.is_array() || json.size() != 3 || !json[0].is_number() || !json[1].is_number() ||
        !json[2].is_number()) {
        spdlog::warn("[{}] Vec3 フィールドが不正なため既定値を使用します", logTag);
        return fallback;
    }
    return { json[0].get<float>(), json[1].get<float>(), json[2].get<float>() };
}

}  // namespace rs::world

//
// SolarModeNames.h - 自転/公転の強制モードと、保存・UIで使う名前の対応表。
//
// 名前は列挙子の並びと同じ順の表を1つだけ持ち、書き出し(列挙→名前)と読み込み(名前→列挙)の
// どちらもこの表を引く。モードを足すときは、列挙子と表の末尾を揃えて足す。
//
#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>

#include "SolarPosition.h"

namespace rs::world {

// .rslの world.environment に書く名前。
inline constexpr std::array<std::string_view, 3> kEarthRotationKeys{ "auto", "day", "night" };
inline constexpr std::array<std::string_view, 5> kEarthRevolutionKeys{ "auto", "spring", "summer", "autumn",
                                                                       "winter" };

static_assert(kEarthRotationKeys.size() == static_cast<size_t>(EarthRotationMode::AlwaysNight) + 1);
static_assert(kEarthRevolutionKeys.size() == static_cast<size_t>(EarthRevolutionMode::Winter) + 1);

// 添字(UIの選択位置や.rs2の整数値)から列挙へ。範囲外はAuto。
inline EarthRotationMode EarthRotationModeFromIndex(int index) {
    return index >= 0 && index < static_cast<int>(kEarthRotationKeys.size()) ? static_cast<EarthRotationMode>(index)
                                                                               : EarthRotationMode::Auto;
}

inline EarthRevolutionMode EarthRevolutionModeFromIndex(int index) {
    return index >= 0 && index < static_cast<int>(kEarthRevolutionKeys.size())
               ? static_cast<EarthRevolutionMode>(index)
               : EarthRevolutionMode::Auto;
}

inline std::string_view EarthRotationModeKey(EarthRotationMode mode) {
    return kEarthRotationKeys[static_cast<size_t>(mode)];
}

inline std::string_view EarthRevolutionModeKey(EarthRevolutionMode mode) {
    return kEarthRevolutionKeys[static_cast<size_t>(mode)];
}

// 知らない名前はnullopt(呼び出し側は今の値を変えない)。
inline std::optional<EarthRotationMode> ParseEarthRotationMode(std::string_view key) {
    for (size_t i = 0; i < kEarthRotationKeys.size(); ++i) {
        if (kEarthRotationKeys[i] == key) return static_cast<EarthRotationMode>(i);
    }
    return std::nullopt;
}

inline std::optional<EarthRevolutionMode> ParseEarthRevolutionMode(std::string_view key) {
    for (size_t i = 0; i < kEarthRevolutionKeys.size(); ++i) {
        if (kEarthRevolutionKeys[i] == key) return static_cast<EarthRevolutionMode>(i);
    }
    return std::nullopt;
}

} // namespace rs::world

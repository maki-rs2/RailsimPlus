//
// FpsLimitChoices.h - FPS上限の選択肢(0 = 無制限)。
//
// 画面の選択肢はこの表の並びと同じ(RenderSettingsRows.cpp の表が文言を作る)。settings.datには整数で保存する。
//
#pragma once

#include <array>

namespace rs::app {

constexpr std::array<int, 6> kFpsLimitChoices = {0, 30, 60, 120, 144, 240};

constexpr int kDefaultFpsLimit = 0;

// 選択肢に無い値は無制限にそろえる。
constexpr int NormalizeFpsLimit(int fpsLimit) {
    for (const int choice : kFpsLimitChoices) {
        if (choice == fpsLimit) return fpsLimit;
    }
    return kDefaultFpsLimit;
}

} // namespace rs::app

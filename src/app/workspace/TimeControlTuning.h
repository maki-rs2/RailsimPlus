#pragma once
#include <array>

namespace rs::app::workspace {
struct TimeControlTuning {
    // 等速・早送り2段の時間倍率。JSONから読み込む。
    std::array<double, 3> scales_{1.0, 10.0, 100.0};
    static const TimeControlTuning& Embedded();
};
}

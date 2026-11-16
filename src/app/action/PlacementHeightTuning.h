#pragma once

namespace rs::app {

// 高さ調整(↑↓キー長押し)の調整値。assets/config/placement_height_tuning.json から読む。
struct PlacementHeightTuning {
    // 押し続けて連続送りに入るまでの待ち時間[秒]。実機で触って調整する値。
    double repeatDelaySeconds = 0.4;
    // 連続送りの速さ[歩/秒]。実機で触って調整する値。
    double repeatStepsPerSecond = 16.0;

    static const PlacementHeightTuning& Embedded();
};

} // namespace rs::app

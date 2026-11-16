#pragma once

#include <cstddef>

namespace rs::sim {

struct ParticleTuning {
    std::size_t maxParticles = 8192;
    double maxStepSeconds = 1.0 / 30.0;
    std::size_t maxSubsteps = 8;
    // 速度と加速度の指数平滑に使うゲーム内秒の時定数。
    double smoothingSeconds = 0.2;
    // 空間ノイズの格子を引く倍率(mの逆数)と、時間による移動速度(格子/秒)。
    double noiseSpatialScale = 0.2;
    double noiseTemporalScale = 0.15;
    // 粒ごとの角速度は、この絶対値以下の一様乱数(rad/s)。
    double maxAngularSpeedRadiansPerSecond = 0.25;

    static const ParticleTuning& Embedded();
};

} // namespace rs::sim

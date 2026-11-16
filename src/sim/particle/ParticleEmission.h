#pragma once

#include <array>
#include <cstddef>

#include "ParticlePool.h"
#include "ParticleRandom.h"

namespace rs::sim {

struct ParticleEmitterState {
    core::DVec3 previousPosition;
    core::DVec3 smoothedVelocity;
    double smoothedSpeed = 0.0;
    double smoothedAcceleration = 0.0;
    double credit = 0.0;
    bool initialized = false;
    bool hasVelocity = false;
};

struct ParticleEmissionFrame {
    core::DVec3 previousPosition;
    core::DVec3 currentPosition;
    core::DVec3 velocity;
    double rate = 0.0;
};

struct ParticleEmissionTimes {
    std::size_t count = 0;
    double firstSeconds = 0.0;
    double intervalSeconds = 0.0;
};

class ParticleEmission {
public:
    static ParticleEmissionFrame Prepare(ParticleEmitterState& state, const ParticleEmitterParams& params,
                                         const core::DVec3& position, double dtSeconds, double smoothingSeconds);
    // 容量を超える放出も端数から消費し、空きができたときのまとめ放出を避ける。
    static ParticleEmissionTimes Schedule(double& credit, double rate, double dtSeconds, std::size_t available);
    // 軸は回転だけを表すワールド座標の単位ベクトル。位置には放出点を渡す。
    static ParticleState Create(const ParticleEmitterParams& params, const std::array<core::DVec3, 3>& axes,
                                const core::DVec3& position, const core::DVec3& emitterVelocity,
                                ParticleRandom& random, double maxAngularSpeedRadiansPerSecond);
};

} // namespace rs::sim

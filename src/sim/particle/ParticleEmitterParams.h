#pragma once

#include <array>

#include "../../core/Math.h"
#include "../../plugins/schema/EffectorSchema.h"

namespace rs::sim {

using ParticleColor = std::array<double, 4>;

// 距離はm、時間は秒。放出率の速度係数はm/s、加速度係数はm/s²を受ける。
struct ParticleEmitterParams {
    double minRate = 0.0;
    double maxRate = 0.0;
    double velocityFactor = 0.0;
    double accelerationFactor = 0.0;
    double decelerationFactor = 0.0;
    std::array<double, 2> lifetimeSeconds{};
    std::array<core::DVec3, 2> directionMetersPerSecond{};
    std::array<double, 2> initialRadiusMeters{};
    std::array<double, 2> finalRadiusMeters{};
    std::array<ParticleColor, 2> colors{};
    double airResistance = 0.0;
    double gravityMetersPerSecondSquared = 0.0;
    double turbulenceMetersPerSecond = 0.0;
    legacy_pi::ParticleBlendMode blendMode = legacy_pi::ParticleBlendMode::Alpha;

    static ParticleEmitterParams FromDefinition(const legacy_pi::ParticleDef& definition);
};

} // namespace rs::sim

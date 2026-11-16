#include "ParticleEmission.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace rs::sim {

ParticleEmissionFrame ParticleEmission::Prepare(ParticleEmitterState& state, const ParticleEmitterParams& params,
                                                const core::DVec3& position, double dtSeconds,
                                                double smoothingSeconds) {
    ParticleEmissionFrame frame;
    frame.previousPosition = state.initialized ? state.previousPosition : position;
    frame.currentPosition = position;
    state.previousPosition = position;
    if (!state.initialized || dtSeconds <= 0.0) {
        state.initialized = true;
        state.hasVelocity = false;
        return frame;
    }
    const core::DVec3 velocity = core::Scale(core::Sub(position, frame.previousPosition), 1.0 / dtSeconds);
    const double speed = core::Length(velocity);
    if (!state.hasVelocity) {
        state.smoothedVelocity = velocity;
        state.smoothedSpeed = speed;
        state.smoothedAcceleration = 0.0;
        state.hasVelocity = true;
    } else {
        const double fraction = smoothingSeconds > 0.0 ? -std::expm1(-dtSeconds / smoothingSeconds) : 1.0;
        const double previousSpeed = state.smoothedSpeed;
        state.smoothedVelocity = core::Add(state.smoothedVelocity,
            core::Scale(core::Sub(velocity, state.smoothedVelocity), fraction));
        state.smoothedSpeed = std::lerp(previousSpeed, speed, fraction);
        const double acceleration = (state.smoothedSpeed - previousSpeed) / dtSeconds;
        state.smoothedAcceleration = std::lerp(state.smoothedAcceleration, acceleration, fraction);
    }
    const double accelerationFactor = state.smoothedAcceleration < 0.0
        ? -params.decelerationFactor : params.accelerationFactor;
    frame.velocity = state.smoothedVelocity;
    frame.rate = std::clamp(params.minRate + params.velocityFactor * state.smoothedSpeed +
        accelerationFactor * state.smoothedAcceleration, params.minRate, params.maxRate);
    return frame;
}

ParticleEmissionTimes ParticleEmission::Schedule(double& credit, double rate, double dtSeconds,
                                                 std::size_t available) {
    if (rate <= 0.0 || dtSeconds <= 0.0) return {};
    const double previousCredit = credit;
    const double total = credit + rate * dtSeconds;
    // 整数境界の丸めで、同じ総時間の放出数が1個減ることを避ける。
    const double tolerance = std::numeric_limits<double>::epsilon() * std::max(1.0, total) * 8.0;
    const double emitted = std::floor(total + tolerance);
    credit = std::max(0.0, total - emitted);
    return {static_cast<std::size_t>(std::min(emitted, static_cast<double>(available))),
            std::min(dtSeconds, (1.0 - previousCredit) / rate), 1.0 / rate};
}

ParticleState ParticleEmission::Create(const ParticleEmitterParams& params,
                                       const std::array<core::DVec3, 3>& axes,
                                       const core::DVec3& position, const core::DVec3& emitterVelocity,
                                       ParticleRandom& random, double maxAngularSpeedRadiansPerSecond) {
    ParticleState particle;
    particle.position = position;
    const core::DVec3& first = params.directionMetersPerSecond[0];
    const core::DVec3& second = params.directionMetersPerSecond[1];
    const core::DVec3 local = {random.Uniform(first.x, second.x), random.Uniform(first.y, second.y),
                              random.Uniform(first.z, second.z)};
    particle.velocity = core::Add(emitterVelocity, core::Add(core::Scale(axes[0], local.x),
        core::Add(core::Scale(axes[1], local.y), core::Scale(axes[2], local.z))));
    particle.lifetimeSeconds = random.Uniform(params.lifetimeSeconds[0], params.lifetimeSeconds[1]);
    particle.initialRadiusMeters = random.Uniform(params.initialRadiusMeters[0], params.initialRadiusMeters[1]);
    particle.finalRadiusMeters = random.Uniform(params.finalRadiusMeters[0], params.finalRadiusMeters[1]);
    const double colorFraction = random.Uniform();
    for (std::size_t index = 0; index < particle.color.size(); ++index) {
        particle.color[index] = std::lerp(params.colors[0][index], params.colors[1][index], colorFraction);
    }
    particle.angleRadians = random.Uniform(0.0, 2.0 * std::numbers::pi);
    particle.angularVelocityRadiansPerSecond = random.Uniform(-maxAngularSpeedRadiansPerSecond,
                                                              maxAngularSpeedRadiansPerSecond);
    particle.dynamics = {params.airResistance, params.gravityMetersPerSecondSquared, params.turbulenceMetersPerSecond};
    particle.blendMode = params.blendMode;
    return particle;
}

} // namespace rs::sim

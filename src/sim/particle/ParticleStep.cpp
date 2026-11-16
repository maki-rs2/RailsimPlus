#include "ParticleStep.h"

#include <algorithm>
#include <cmath>

namespace rs::sim {
namespace {

void Integrate(ParticleState& particle, const core::DVec3& target, double dtSeconds) {
    const double resistance = std::clamp(particle.dynamics.airResistance, 0.0, 1.0);
    const core::DVec3 gravity = {0.0, -particle.dynamics.gravityMetersPerSecondSquared, 0.0};
    if (resistance == 1.0) {
        particle.velocity = target;
        particle.position = core::Add(particle.position, core::Scale(target, dtSeconds));
        return;
    }
    const double decayRate = -std::log1p(-resistance);
    double velocityIntegral = dtSeconds;
    double gravityIntegral = dtSeconds * dtSeconds * 0.5;
    const double exponent = decayRate * dtSeconds;
    if (exponent > 0.0) {
        velocityIntegral = -std::expm1(-exponent) / decayRate;
        // 小さい指数では差の桁落ちを避け、同じ解析式の級数で位置を求める。
        constexpr double kSeriesLimit = 1e-4;
        gravityIntegral = exponent < kSeriesLimit
            ? dtSeconds * dtSeconds * (0.5 - exponent / 6.0 + exponent * exponent / 24.0)
            : (dtSeconds - velocityIntegral) / decayRate;
    }
    const core::DVec3 relative = core::Sub(particle.velocity, target);
    particle.position = core::Add(particle.position, core::Add(core::Scale(target, dtSeconds),
        core::Add(core::Scale(relative, velocityIntegral), core::Scale(gravity, gravityIntegral))));
    particle.velocity = core::Add(target, core::Add(core::Scale(relative, std::exp(-exponent)),
                                                  core::Scale(gravity, velocityIntegral)));
}

} // namespace

bool ParticleStep::Advance(ParticleState& particle, double dtSeconds, double timeSeconds,
                           const ParticleEnvironment& environment, const ParticleNoise& noise,
                           const ParticleTuning& tuning) {
    if (particle.lifetimeSeconds <= 0.0 || particle.ageSeconds >= particle.lifetimeSeconds) return false;
    if (dtSeconds <= 0.0) return true;
    particle.ageSeconds += dtSeconds;
    if (particle.ageSeconds >= particle.lifetimeSeconds) return false;
    core::DVec3 target = environment.windEnabled ? environment.windMetersPerSecond : core::DVec3{};
    if (particle.dynamics.turbulenceMetersPerSecond != 0.0) {
        const core::DVec3 perturbation = noise.Sample(core::Scale(particle.position, tuning.noiseSpatialScale),
                                                     timeSeconds * tuning.noiseTemporalScale);
        target = core::Add(target, core::Scale(perturbation, particle.dynamics.turbulenceMetersPerSecond));
    }
    Integrate(particle, target, dtSeconds);
    particle.angleRadians += particle.angularVelocityRadiansPerSecond * dtSeconds;
    return true;
}

void ParticleStep::Update(ParticlePool& pool, double dtSeconds, double timeSeconds,
                          const ParticleEnvironment& environment, const ParticleNoise& noise,
                          const ParticleTuning& tuning) {
    for (std::size_t index = 0; index < pool.Size();) {
        ParticleState particle = pool.Get(index);
        if (Advance(particle, dtSeconds, timeSeconds, environment, noise, tuning)) {
            pool.Set(index, particle);
            ++index;
        } else {
            pool.Remove(index);
        }
    }
}

void ParticleStep::AgeOnly(ParticlePool& pool, double dtSeconds) {
    for (std::size_t index = 0; index < pool.Size();) {
        ParticleState particle = pool.Get(index);
        particle.ageSeconds += dtSeconds;
        if (particle.ageSeconds >= particle.lifetimeSeconds) {
            pool.Remove(index);
        } else {
            pool.Set(index, particle);
            ++index;
        }
    }
}

} // namespace rs::sim

#pragma once

#include <cstddef>
#include <vector>

#include "ParticleEmitterParams.h"

namespace rs::sim {

struct ParticleDynamics {
    double airResistance = 0.0;
    double gravityMetersPerSecondSquared = 0.0;
    double turbulenceMetersPerSecond = 0.0;
};

struct ParticleState {
    core::DVec3 position;
    core::DVec3 velocity;
    double ageSeconds = 0.0;
    double lifetimeSeconds = 0.0;
    double initialRadiusMeters = 0.0;
    double finalRadiusMeters = 0.0;
    ParticleColor color{};
    double angleRadians = 0.0;
    double angularVelocityRadiansPerSecond = 0.0;
    ParticleDynamics dynamics;
    legacy_pi::ParticleBlendMode blendMode = legacy_pi::ParticleBlendMode::Alpha;

    double Alpha() const;
    double RadiusMeters() const;
};

// 各列は生成時に容量を固定する。削除後の添字は、同じ粒を指すとは限らない。
class ParticlePool {
public:
    explicit ParticlePool(std::size_t capacity);
    std::size_t Size() const { return size_; }
    std::size_t Capacity() const { return positions_.size(); }
    bool Add(const ParticleState& particle);
    void Remove(std::size_t index);
    ParticleState Get(std::size_t index) const;
    void Set(std::size_t index, const ParticleState& particle);

private:
    std::size_t size_ = 0;
    std::vector<core::DVec3> positions_;
    std::vector<core::DVec3> velocities_;
    std::vector<double> agesSeconds_;
    std::vector<double> lifetimesSeconds_;
    std::vector<double> initialRadiiMeters_;
    std::vector<double> finalRadiiMeters_;
    std::vector<ParticleColor> colors_;
    std::vector<double> anglesRadians_;
    std::vector<double> angularVelocitiesRadiansPerSecond_;
    std::vector<ParticleDynamics> dynamics_;
    std::vector<legacy_pi::ParticleBlendMode> blendModes_;
};

} // namespace rs::sim

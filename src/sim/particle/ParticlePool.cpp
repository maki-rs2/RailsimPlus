#include "ParticlePool.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace rs::sim {

double ParticleState::Alpha() const {
    return lifetimeSeconds > 0.0 ? std::clamp(1.0 - ageSeconds / lifetimeSeconds, 0.0, 1.0) : 0.0;
}

double ParticleState::RadiusMeters() const {
    return std::lerp(finalRadiusMeters, initialRadiusMeters, Alpha());
}

ParticlePool::ParticlePool(std::size_t capacity)
    : positions_(capacity), velocities_(capacity), agesSeconds_(capacity), lifetimesSeconds_(capacity),
      initialRadiiMeters_(capacity), finalRadiiMeters_(capacity), colors_(capacity), anglesRadians_(capacity),
      angularVelocitiesRadiansPerSecond_(capacity), dynamics_(capacity), blendModes_(capacity) {}

bool ParticlePool::Add(const ParticleState& particle) {
    if (size_ == Capacity()) return false;
    Set(size_, particle);
    ++size_;
    return true;
}

void ParticlePool::Remove(std::size_t index) {
    assert(index < size_);
    --size_;
    if (index != size_) Set(index, Get(size_));
}

ParticleState ParticlePool::Get(std::size_t index) const {
    assert(index < Capacity());
    return {positions_[index], velocities_[index], agesSeconds_[index], lifetimesSeconds_[index],
            initialRadiiMeters_[index], finalRadiiMeters_[index], colors_[index], anglesRadians_[index],
            angularVelocitiesRadiansPerSecond_[index], dynamics_[index], blendModes_[index]};
}

void ParticlePool::Set(std::size_t index, const ParticleState& particle) {
    assert(index < Capacity());
    positions_[index] = particle.position;
    velocities_[index] = particle.velocity;
    agesSeconds_[index] = particle.ageSeconds;
    lifetimesSeconds_[index] = particle.lifetimeSeconds;
    initialRadiiMeters_[index] = particle.initialRadiusMeters;
    finalRadiiMeters_[index] = particle.finalRadiusMeters;
    colors_[index] = particle.color;
    anglesRadians_[index] = particle.angleRadians;
    angularVelocitiesRadiansPerSecond_[index] = particle.angularVelocityRadiansPerSecond;
    dynamics_[index] = particle.dynamics;
    blendModes_[index] = particle.blendMode;
}

} // namespace rs::sim

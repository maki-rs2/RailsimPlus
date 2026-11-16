#pragma once

#include "ParticleNoise.h"
#include "ParticlePool.h"
#include "ParticleTuning.h"

namespace rs::sim {

struct ParticleEnvironment {
    bool windEnabled = false;
    core::DVec3 windMetersPerSecond;
};

class ParticleStep {
public:
    static bool Advance(ParticleState& particle, double dtSeconds, double timeSeconds,
                        const ParticleEnvironment& environment, const ParticleNoise& noise,
                        const ParticleTuning& tuning);
    static void Update(ParticlePool& pool, double dtSeconds, double timeSeconds,
                       const ParticleEnvironment& environment, const ParticleNoise& noise,
                       const ParticleTuning& tuning);
    static void AgeOnly(ParticlePool& pool, double dtSeconds);
};

} // namespace rs::sim

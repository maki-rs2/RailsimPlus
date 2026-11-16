#include "ParticleEmitterParams.h"

#include "../../core/SpeedUnits.h"

#include <algorithm>

namespace rs::sim {

ParticleEmitterParams ParticleEmitterParams::FromDefinition(const legacy_pi::ParticleDef& definition) {
    ParticleEmitterParams params;
    params.minRate = std::max(0.0, definition.minQty);
    params.maxRate = std::max(params.minRate, definition.maxQty);
    params.velocityFactor = core::MetersPerSecondToKilometersPerHour(definition.velocityRel);
    params.accelerationFactor = core::MetersPerSecondToKilometersPerHour(definition.accelerationRel);
    params.decelerationFactor = core::MetersPerSecondToKilometersPerHour(definition.decelerationRel);
    params.lifetimeSeconds = definition.lifetime;
    params.initialRadiusMeters = definition.initialRadius;
    params.finalRadiusMeters = definition.finalRadius;
    for (std::size_t index = 0; index < definition.direction.size(); ++index) {
        const core::Vec3& direction = definition.direction[index];
        params.directionMetersPerSecond[index] = {static_cast<double>(direction.x),
            static_cast<double>(direction.y), static_cast<double>(direction.z)};
        const legacy_pi::PiColor& color = definition.color[index];
        params.colors[index] = {static_cast<double>(color.r), static_cast<double>(color.g),
            static_cast<double>(color.b), static_cast<double>(color.a)};
    }
    params.airResistance = std::clamp(definition.airResistance, 0.0, 1.0);
    params.gravityMetersPerSecondSquared = definition.gravity;
    params.turbulenceMetersPerSecond = definition.turbulence;
    params.blendMode = definition.blendMode;
    return params;
}

} // namespace rs::sim

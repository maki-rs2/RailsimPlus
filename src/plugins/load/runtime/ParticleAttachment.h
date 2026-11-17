#pragma once

#include <string>
#include <vector>

#include "../../../sim/particle/ParticleEmitterParams.h"

namespace rs::legacy_pi { struct PluginDefinition; }

namespace rs::app {

struct ParticleAttachment {
    int partDefIndex = -1;
    rs::core::Vec3 sourceCoord{0, 0, 0};
    rs::sim::ParticleEmitterParams params;
    std::string texturePath;
    rs::legacy_pi::TriggerInfo trig;
};

void BuildParticleAttachments(const rs::legacy_pi::PluginDefinition& def, const std::string& folderUtf8,
                               std::vector<ParticleAttachment>& out);

} // namespace rs::app

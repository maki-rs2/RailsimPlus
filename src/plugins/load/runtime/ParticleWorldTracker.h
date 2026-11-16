#pragma once

#include <array>
#include <vector>

#include "ParticleAttachment.h"

namespace rs::app {

struct ParticleRuntimeState {
    rs::core::Vec3 worldPos{0, 0, 0};
    std::array<rs::core::Vec3, 3> worldAxes = {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
    bool hasWorld = false;
};

void UpdateParticleWorldPoses(const std::vector<ParticleAttachment>& attachments,
                               const std::vector<rs::core::Mat4>& dynamicWorlds,
                               const rs::core::Mat4& entityWorld, std::vector<ParticleRuntimeState>& states);

} // namespace rs::app

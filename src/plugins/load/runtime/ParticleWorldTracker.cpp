#include "ParticleWorldTracker.h"

#include <algorithm>
#include <cstddef>

#include "AttachmentWorldPose.h"

namespace rs::app {

void UpdateParticleWorldPoses(const std::vector<ParticleAttachment>& attachments,
                               const std::vector<rs::core::Mat4>& dynamicWorlds,
                               const rs::core::Mat4& entityWorld, std::vector<ParticleRuntimeState>& states) {
    const std::size_t count = std::min(attachments.size(), states.size());
    for (std::size_t index = 0; index < count; ++index) {
        const ParticleAttachment& attachment = attachments[index];
        if (attachment.partDefIndex < 0 || static_cast<std::size_t>(attachment.partDefIndex) >= dynamicWorlds.size())
            continue;
        const AttachmentWorldPose pose = ComputeAttachmentWorldPose(
            attachment.sourceCoord, dynamicWorlds[static_cast<std::size_t>(attachment.partDefIndex)], entityWorld);
        states[index] = {pose.worldPos, pose.worldAxes, true};
    }
}

} // namespace rs::app

// HeadlightWorldTracker.cpp - Headlight(灯具)のワールド位置と向きの追跡。
#include "HeadlightWorldTracker.h"

#include <algorithm>

#include "AttachmentWorldPose.h"

namespace rs::app {

HeadlightRuntimeState ComputeHeadlightWorldPose(const HeadlightAttachment& attachment,
                                                const rs::core::Mat4& partWorld,
                                                const rs::core::Mat4& entityWorld) {
    const AttachmentWorldPose pose = ComputeAttachmentWorldPose(attachment.sourceCoord, partWorld, entityWorld);
    const rs::core::Vec3& dir = attachment.direction;
    HeadlightRuntimeState state;
    state.worldPos = pose.worldPos;
    state.worldDir = (pose.worldAxes[0] * dir.x + pose.worldAxes[1] * dir.y + pose.worldAxes[2] * dir.z).Normalized();
    state.hasWorld = true;
    return state;
}

void UpdateHeadlightWorldPoses(const std::vector<HeadlightAttachment>& attachments,
                               const std::vector<rs::core::Mat4>& dynamicWorlds,
                               const rs::core::Mat4& entityWorld, std::vector<HeadlightRuntimeState>& states) {
    const size_t updateCount = std::min(attachments.size(), states.size());
    for (size_t i = 0; i < updateCount; ++i) {
        const HeadlightAttachment& attachment = attachments[i];
        if (attachment.partDefIndex < 0 || static_cast<size_t>(attachment.partDefIndex) >= dynamicWorlds.size())
            continue;
        states[i] = ComputeHeadlightWorldPose(attachment, dynamicWorlds[static_cast<size_t>(attachment.partDefIndex)],
                                              entityWorld);
    }
}

} // namespace rs::app

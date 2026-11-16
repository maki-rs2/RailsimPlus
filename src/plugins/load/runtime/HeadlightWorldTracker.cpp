// HeadlightWorldTracker.cpp - Headlight(灯具)のワールド位置と向きの追跡。
#include "HeadlightWorldTracker.h"

#include <algorithm>

namespace rs::app {
namespace {

rs::core::Vec3 Row(const rs::core::Mat4& matrix, size_t row) {
    return rs::core::Vec3{matrix.m[row * 4], matrix.m[row * 4 + 1], matrix.m[row * 4 + 2]};
}

} // namespace

HeadlightRuntimeState ComputeHeadlightWorldPose(const HeadlightAttachment& attachment,
                                                const rs::core::Mat4& partWorld,
                                                const rs::core::Mat4& entityWorld) {
    const rs::core::Mat4 world = rs::core::Mat4::Mul(partWorld, entityWorld);
    const rs::core::Vec3 right = Row(world, 0).Normalized();
    const rs::core::Vec3 up = Row(world, 1).Normalized();
    const rs::core::Vec3 forward = Row(world, 2).Normalized();
    const rs::core::Vec3& local = attachment.sourceCoord;
    const rs::core::Vec3& dir = attachment.direction;
    HeadlightRuntimeState state;
    state.worldPos = Row(world, 3) + right * local.x + up * local.y + forward * local.z;
    state.worldDir = (right * dir.x + up * dir.y + forward * dir.z).Normalized();
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

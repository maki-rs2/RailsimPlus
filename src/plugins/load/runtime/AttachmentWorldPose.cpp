#include "AttachmentWorldPose.h"

#include <cstddef>

namespace rs::app {
namespace {

rs::core::Vec3 Row(const rs::core::Mat4& matrix, std::size_t row) {
    return {matrix.m[row * 4], matrix.m[row * 4 + 1], matrix.m[row * 4 + 2]};
}

} // namespace

AttachmentWorldPose ComputeAttachmentWorldPose(const rs::core::Vec3& sourceCoord,
                                                const rs::core::Mat4& partWorld,
                                                const rs::core::Mat4& entityWorld) {
    const rs::core::Mat4 world = rs::core::Mat4::Mul(partWorld, entityWorld);
    AttachmentWorldPose pose;
    for (std::size_t axis = 0; axis < pose.worldAxes.size(); ++axis) {
        pose.worldAxes[axis] = Row(world, axis).Normalized();
    }
    pose.worldPos = Row(world, 3) + pose.worldAxes[0] * sourceCoord.x +
                    pose.worldAxes[1] * sourceCoord.y + pose.worldAxes[2] * sourceCoord.z;
    return pose;
}

} // namespace rs::app

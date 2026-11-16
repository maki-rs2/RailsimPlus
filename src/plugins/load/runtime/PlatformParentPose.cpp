#include "../../../render/RenderPrecision.h"
//
// PlatformParentPose.cpp - Platform の ParentObject の姿勢に追従する点の計算。
//
#include "PlatformParentPose.h"

#include "../../../world/alignment/RigidPose.h"

namespace rs::app {

rs::core::Vec3 TransformPlatformParentPoint(const rs::core::Mat4& parentWorld,
                                            const rs::core::Vec3& coord) {
    const rs::core::DVec3 p = rs::world::ApplyToPoint(
        rs::world::NormalizedRigidPose(parentWorld), {coord.x, coord.y, coord.z});
    return rs::render::ToGpuFloat(p);
}

} // namespace rs::app

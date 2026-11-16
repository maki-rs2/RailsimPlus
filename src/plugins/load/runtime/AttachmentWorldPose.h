#pragma once

#include <array>

#include "../../../core/Math.h"

namespace rs::app {

struct AttachmentWorldPose {
    rs::core::Vec3 worldPos{0, 0, 0};
    std::array<rs::core::Vec3, 3> worldAxes;
};

// SourceCoordは、拡縮を除いた部品の右・上・前に沿う距離として扱う。
AttachmentWorldPose ComputeAttachmentWorldPose(const rs::core::Vec3& sourceCoord,
                                                const rs::core::Mat4& partWorld,
                                                const rs::core::Mat4& entityWorld);

} // namespace rs::app

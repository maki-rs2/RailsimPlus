#include "TrainPickBox.h"

#include <algorithm>
#include <cfloat>

namespace rs::app {

std::vector<PartLocalBounds> ComputePartLocalBounds(
    const std::vector<rs::render::MeshVertex>& vertices, size_t partCount) {
    std::vector<PartLocalBounds> bounds(partCount);
    for (const rs::render::MeshVertex& vertex : vertices) {
        if (vertex.partIndex >= partCount) continue;
        PartLocalBounds& part = bounds[vertex.partIndex];
        for (int axis = 0; axis < 3; ++axis) {
            if (!part.valid) {
                part.min[axis] = part.max[axis] = vertex.pos[axis];
            } else {
                part.min[axis] = std::min(part.min[axis], vertex.pos[axis]);
                part.max[axis] = std::max(part.max[axis], vertex.pos[axis]);
            }
        }
        part.valid = true;
    }
    return bounds;
}

TrainPickBox BuildTrainPickBox(const std::vector<PartLocalBounds>& bounds,
                               const std::vector<rs::core::Mat4>& partWorlds,
                               const std::vector<bool>& included, const rs::core::Mat4& frame) {
    TrainPickBox box;
    float lo[3] = {FLT_MAX, FLT_MAX, FLT_MAX};
    float hi[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
    const rs::core::Mat4 inverseFrame = rs::core::InverseRigid(frame);
    const size_t count = std::min({bounds.size(), partWorlds.size(), included.size()});
    for (size_t i = 0; i < count; ++i) {
        if (!included[i] || !bounds[i].valid) continue;
        const rs::core::Mat4 toFrame = rs::core::Mat4::Mul(partWorlds[i], inverseFrame);
        for (int corner = 0; corner < 8; ++corner) {
            const float point[4] = {(corner & 1) ? bounds[i].max[0] : bounds[i].min[0],
                                    (corner & 2) ? bounds[i].max[1] : bounds[i].min[1],
                                    (corner & 4) ? bounds[i].max[2] : bounds[i].min[2], 1.0f};
            float local[4];
            rs::core::TransformPoint4(toFrame, point, local);
            for (int axis = 0; axis < 3; ++axis) {
                lo[axis] = std::min(lo[axis], local[axis]);
                hi[axis] = std::max(hi[axis], local[axis]);
            }
            box.valid = true;
        }
    }
    if (!box.valid) return box;
    for (int axis = 0; axis < 3; ++axis) {
        box.aabbMin[axis] = lo[axis];
        box.aabbMax[axis] = hi[axis];
    }
    return box;
}

} // namespace rs::app

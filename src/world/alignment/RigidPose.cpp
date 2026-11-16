#include "RigidPose.h"

#include "../../core/Epsilon.h"

#include <cmath>

namespace rs::world {

bool RigidPose::IsIdentity() const {
    const double identity[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    for (int i = 0; i < 9; ++i)
        if (std::abs(r[i] - identity[i]) > rs::core::kRigidRotationIdentityEps) return false;
    return std::abs(t.x) <= rs::core::kRigidTranslationIdentityEpsM && std::abs(t.y) <= rs::core::kRigidTranslationIdentityEpsM && std::abs(t.z) <= rs::core::kRigidTranslationIdentityEpsM;
}

RigidPose NormalizedRigidPose(const rs::core::Mat4& matrix) {
    RigidPose pose;
    for (size_t row = 0; row < 3; ++row) {
        const rs::core::DVec3 axis = rs::core::Normalize(
            {matrix.m[row * 4], matrix.m[row * 4 + 1], matrix.m[row * 4 + 2]});
        pose.r[row * 3] = axis.x;
        pose.r[row * 3 + 1] = axis.y;
        pose.r[row * 3 + 2] = axis.z;
    }
    pose.t = {matrix.m[12], matrix.m[13], matrix.m[14]};
    return pose;
}

rs::core::DVec3 ApplyToDirection(const RigidPose& pose, const rs::core::DVec3& d) {
    return {d.x * pose.r[0] + d.y * pose.r[3] + d.z * pose.r[6],
            d.x * pose.r[1] + d.y * pose.r[4] + d.z * pose.r[7],
            d.x * pose.r[2] + d.y * pose.r[5] + d.z * pose.r[8]};
}

rs::core::DVec3 ApplyToPoint(const RigidPose& pose, const rs::core::DVec3& p) {
    return rs::core::Add(ApplyToDirection(pose, p), pose.t);
}

RigidPose RigidDelta(const RigidPose& rest, const RigidPose& now) {
    // rest の逆: p = (q - t) * R^T なので R_inv = R^T、t_inv = -t * R^T。
    RigidPose inverse;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) inverse.r[i * 3 + j] = rest.r[j * 3 + i];
    inverse.t = ApplyToDirection(inverse, {-rest.t.x, -rest.t.y, -rest.t.z});
    // 合成 p * inverse * now: R = R_inv * R_now、t = t_inv * R_now + t_now。
    RigidPose delta;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            double sum = 0.0;
            for (int k = 0; k < 3; ++k) sum += inverse.r[i * 3 + k] * now.r[k * 3 + j];
            delta.r[i * 3 + j] = sum;
        }
    delta.t = rs::core::Add(ApplyToDirection(now, inverse.t), now.t);
    return delta;
}

} // namespace rs::world

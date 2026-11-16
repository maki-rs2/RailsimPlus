//
// RigidPose.h - 剛体変換(回転+平行移動、double)。親オブジェクトに追従するレールの差分に使う。
//
#pragma once

#include "../../core/Math.h"

namespace rs::world {

// row-vector 慣習(p' = p * R + t)の剛体変換。r は行優先の3x3(各行が基底軸の行き先)。
struct RigidPose {
    double r[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    rs::core::DVec3 t{};

    bool IsIdentity() const;
};

// 4x4行列から剛体変換を作る。3本の軸はスケールを除いて長さ1にそろえ、平行移動はそのまま使う
// (RailSim II の NormalizeMatrix と同じ)。軸の直交は前提とする。float→doubleの境界はここ。
RigidPose NormalizedRigidPose(const rs::core::Mat4& matrix);

// restの姿勢から見たnowの姿勢への差分。ワールド上の点 p(rest 姿勢に属する)を、now の姿勢に属する点へ動かす:
// Apply(Delta(rest, now), p) == Apply(now, ApplyInverse(rest, p))。
RigidPose RigidDelta(const RigidPose& rest, const RigidPose& now);

rs::core::DVec3 ApplyToPoint(const RigidPose& pose, const rs::core::DVec3& point);
rs::core::DVec3 ApplyToDirection(const RigidPose& pose, const rs::core::DVec3& direction);

} // namespace rs::world

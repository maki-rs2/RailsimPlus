#pragma once

#include "Math.h"

namespace rs::core {

struct Frustum {
    // 各平面 (a,b,c,d): a*x + b*y + c*z + d >= 0 が視錐台の内側。正規化済み。
    float planes[6][4] = {};
};

// row-vector 慣習 (v' = v * M) の viewProj から6平面を抽出する。
Frustum ExtractFrustum(const Mat4& viewProj);

// 軸平行境界ボックスが視錐台と交差するなら true。完全に外側なら false。
bool FrustumIntersectsAabb(const Frustum& frustum, const float aabbMin[3], const float aabbMax[3]);

} // namespace rs::core

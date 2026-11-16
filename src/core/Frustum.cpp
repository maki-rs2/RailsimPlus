#include "Frustum.h"

#include "Epsilon.h"

#include <cmath>

namespace rs::core {
namespace {

void NormalizePlane(float plane[4]) {
    const float length = std::sqrt(plane[0] * plane[0] + plane[1] * plane[1] +
                                   plane[2] * plane[2]);
    if (length <= rs::core::kNormalizeLengthEps) return;
    for (int i = 0; i < 4; ++i) plane[i] /= length;
}

void AddColumns(const Mat4& matrix, int first, float sign, int second, float out[4]) {
    for (int row = 0; row < 4; ++row)
        out[row] = matrix.m[row * 4 + first] + sign * matrix.m[row * 4 + second];
    NormalizePlane(out);
}

} // namespace

Frustum ExtractFrustum(const Mat4& viewProj) {
    Frustum frustum{};
    AddColumns(viewProj, 3, 1.0f, 0, frustum.planes[0]);
    AddColumns(viewProj, 3, -1.0f, 0, frustum.planes[1]);
    AddColumns(viewProj, 3, 1.0f, 1, frustum.planes[2]);
    AddColumns(viewProj, 3, -1.0f, 1, frustum.planes[3]);
    // reversed-Z では近遠の名称だけが入れ替わるが、半空間の集合は同一である。
    for (int row = 0; row < 4; ++row) frustum.planes[4][row] = viewProj.m[row * 4 + 2];
    NormalizePlane(frustum.planes[4]);
    AddColumns(viewProj, 3, -1.0f, 2, frustum.planes[5]);
    return frustum;
}

bool FrustumIntersectsAabb(const Frustum& frustum, const float aabbMin[3], const float aabbMax[3]) {
    for (const float* plane : frustum.planes) {
        const float vertex[3] = {
            plane[0] >= 0.0f ? aabbMax[0] : aabbMin[0],
            plane[1] >= 0.0f ? aabbMax[1] : aabbMin[1],
            plane[2] >= 0.0f ? aabbMax[2] : aabbMin[2],
        };
        if (plane[0] * vertex[0] + plane[1] * vertex[1] + plane[2] * vertex[2] + plane[3] < 0.0f)
            return false;
    }
    return true;
}

} // namespace rs::core

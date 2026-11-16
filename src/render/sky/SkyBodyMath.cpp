#include "SkyBodyMath.h"

#include "../../core/Epsilon.h"

namespace rs::render {

using rs::core::Mat4;
using rs::core::Vec3;

Mat4 MakeSkyBodyRotation(const Vec3& forward, const Vec3& upHint) {
    const Vec3 zAxis = forward.Normalized();
    Vec3 xAxis = Vec3::Cross(upHint, zAxis);
    if (xAxis.Length() < rs::core::kHorizontalDirectionEps) {
        // upHint が前方と平行: 別の軸を目安にする。
        const Vec3 fallbackUp = std::fabs(zAxis.x) < 0.9f ? Vec3(1.0f, 0.0f, 0.0f) : Vec3(0.0f, 0.0f, 1.0f);
        xAxis = Vec3::Cross(fallbackUp, zAxis);
    }
    xAxis = xAxis.Normalized();
    const Vec3 yAxis = Vec3::Cross(zAxis, xAxis);
    Mat4 rotation = Mat4::Identity();
    rotation.m[0] = xAxis.x; rotation.m[1] = xAxis.y; rotation.m[2] = xAxis.z;
    rotation.m[4] = yAxis.x; rotation.m[5] = yAxis.y; rotation.m[6] = yAxis.z;
    rotation.m[8] = zAxis.x; rotation.m[9] = zAxis.y; rotation.m[10] = zAxis.z;
    return rotation;
}

Mat4 MakeSkyBodyMvp(const Mat4& view, const Mat4& proj, const Vec3& directionToBody, const Mat4& rotation,
                    float scale) {
    Mat4 world = rotation;
    for (int i = 0; i < 12; ++i) world.m[i] *= scale;  // 回転の3行(x・y・z軸)にscaleを掛ける
    const Vec3 position = directionToBody.Normalized() * kSkyBodyDistance;
    world.m[12] = position.x;
    world.m[13] = position.y;
    world.m[14] = position.z;

    Mat4 viewRotationOnly = view;
    viewRotationOnly.m[12] = viewRotationOnly.m[13] = viewRotationOnly.m[14] = 0.0f;
    return Mat4::Mul(Mat4::Mul(world, viewRotationOnly), proj);
}

} // namespace rs::render

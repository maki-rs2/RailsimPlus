#include "../RenderPrecision.h"
#include "CascadeFit.h"

#include <algorithm>
#include <cmath>

namespace rs::render {

namespace {

using rs::core::Add;
using rs::core::Cross;
using rs::core::DVec3;
using rs::core::Dot;
using rs::core::Length;
using rs::core::Normalize;
using rs::core::Scale;
using rs::core::Sub;

// 光の向きがほぼ真上・真下のときは、基準の上方向を別の軸へ替える(外積が潰れるのを避ける)。
constexpr double kParallelToUpLimit = 0.999;

// 視錐台の一部(カメラ前方向の距離 nearDistance..farDistance)を包む球を、カメラ空間の軸上に置いて求める。
// 近い側の角と遠い側の角が球の中心から等距離になる位置が最小の球。中心が遠い側の面より奥へ出る
// (画角が極端に広い)ときは、遠い側の面の中心に球を置く。
struct FrustumSphere {
    double centerDistance = 0.0; // カメラからの前方向の距離
    double radius = 0.0;
};

FrustumSphere BoundFrustumSlice(double nearDistance, double farDistance, double tanHalfX,
                                double tanHalfY) {
    const double cornerFactor = tanHalfX * tanHalfX + tanHalfY * tanHalfY;
    double center = 0.5 * (farDistance + nearDistance) * (1.0 + cornerFactor);
    center = std::min(center, farDistance);
    const double axial = farDistance - center;
    FrustumSphere sphere;
    sphere.centerDistance = center;
    sphere.radius = std::sqrt(axial * axial + farDistance * farDistance * cornerFactor);
    return sphere;
}

} // namespace

std::array<float, kMaxShadowCascades> ComputeCascadeSplits(float nearDistance, float farDistance,
                                                           std::uint32_t cascadeCount, float lambda) {
    std::array<float, kMaxShadowCascades> splits = {};
    const std::uint32_t count = std::clamp<std::uint32_t>(cascadeCount, 1, kMaxShadowCascades);
    const double far = farDistance;
    const double logNear = far > kSplitNearFloorM ? std::max<double>(nearDistance, kSplitNearFloorM)
                                                  : static_cast<double>(nearDistance);
    const double mix = std::clamp<double>(lambda, 0.0, 1.0);
    for (std::uint32_t i = 0; i < count; ++i) {
        const double ratio = static_cast<double>(i + 1) / static_cast<double>(count);
        const double logSplit = logNear * std::pow(far / logNear, ratio);
        const double linearSplit = std::lerp(static_cast<double>(nearDistance), far, ratio);
        splits[i] = rs::render::ToGpuFloat(mix * logSplit + (1.0 - mix) * linearSplit);
    }
    splits[count - 1] = farDistance;
    return splits;
}

CascadeFitOutput FitCascades(const CascadeFitInput& input) {
    CascadeFitOutput output;
    const double nearDistance = input.nearDistance;
    const double farDistance =
        std::min<double>(input.farDistance, input.shadowDistance);
    if (!(nearDistance > 0.0) || !(farDistance > nearDistance) || !(input.aspect > 0.0f) ||
        !(input.fovYRadians > 0.0f) || input.shadowMapSize == 0 || input.cascadeCount == 0 ||
        input.cascadeCount > kMaxShadowCascades) {
        return output;
    }
    const DVec3 lightDirection = Normalize({input.lightDirection.x, input.lightDirection.y,
                                            input.lightDirection.z});
    if (Length(lightDirection) <= 0.0) return output;

    // カメラのワールド位置と前方向(ビュー行列の逆の行)。
    const rs::core::Mat4 cameraToWorld = rs::core::InverseRigid(input.view);
    const DVec3 cameraPosition = {cameraToWorld.m[12], cameraToWorld.m[13], cameraToWorld.m[14]};
    const DVec3 cameraForward =
        Normalize({cameraToWorld.m[8], cameraToWorld.m[9], cameraToWorld.m[10]});

    const double tanHalfY = std::tan(0.5 * static_cast<double>(input.fovYRadians));
    const double tanHalfX = tanHalfY * static_cast<double>(input.aspect);

    // ライト空間の軸。左手系の LookAt と同じ作りで、Z が光の進む向き。
    DVec3 worldUp = {0.0, 1.0, 0.0};
    if (std::abs(Dot(lightDirection, worldUp)) > kParallelToUpLimit) worldUp = {0.0, 0.0, 1.0};
    const DVec3 axisX = Normalize(Cross(worldUp, lightDirection));
    const DVec3 axisY = Cross(lightDirection, axisX);

    const std::array<float, kMaxShadowCascades> splits = ComputeCascadeSplits(
        input.nearDistance, rs::render::ToGpuFloat(farDistance), input.cascadeCount, input.splitLambda);
    const double blendFraction = std::clamp<double>(input.blendFraction, 0.0, 0.5);
    double rangeNear = nearDistance;
    for (std::uint32_t index = 0; index < input.cascadeCount; ++index) {
        const double rangeFar = splits[index];
        // 奥側のカスケードは、手前のカスケードのブレンド範囲の分だけ手前から覆う。
        double sliceNear = rangeNear;
        if (index > 0) {
            const double previousStart = index > 1 ? static_cast<double>(splits[index - 2]) : nearDistance;
            sliceNear = std::max(nearDistance, rangeNear - blendFraction * (rangeNear - previousStart));
        }
        const FrustumSphere sphere = BoundFrustumSlice(sliceNear, rangeFar, tanHalfX, tanHalfY);
        const DVec3 sphereCenter = Add(cameraPosition, Scale(cameraForward, sphere.centerDistance));

        // XY は球の中心をテクセルの整数倍へ切り下げる。切り下げで中心が最大1テクセル動くので、
        // その分を半径に足して、元の球をいつも覆うようにする。
        const double texelWorld = 2.0 * sphere.radius / static_cast<double>(input.shadowMapSize);
        const double radius = sphere.radius + texelWorld;
        const double snappedTexel = 2.0 * radius / static_cast<double>(input.shadowMapSize);
        const double centerX = std::floor(Dot(sphereCenter, axisX) / snappedTexel) * snappedTexel;
        const double centerY = std::floor(Dot(sphereCenter, axisY) / snappedTexel) * snappedTexel;
        const double centerZ = Dot(sphereCenter, lightDirection);

        // Z はライト側へ casterExtensionM だけ延ばす。光は +Z へ進むので、ライト側は Z が小さい方。
        const double zNear = centerZ - radius - static_cast<double>(input.casterExtensionM);
        const double zFar = centerZ + radius;
        const double zRange = zFar - zNear;

        CascadeFitCascade& cascade = output.cascades[index];
        float* m = cascade.viewProj.m;
        m[0] = rs::render::ToGpuFloat(axisX.x / radius);
        m[1] = rs::render::ToGpuFloat(axisY.x / radius);
        m[2] = rs::render::ToGpuFloat(lightDirection.x / zRange);
        m[3] = 0.0f;
        m[4] = rs::render::ToGpuFloat(axisX.y / radius);
        m[5] = rs::render::ToGpuFloat(axisY.y / radius);
        m[6] = rs::render::ToGpuFloat(lightDirection.y / zRange);
        m[7] = 0.0f;
        m[8] = rs::render::ToGpuFloat(axisX.z / radius);
        m[9] = rs::render::ToGpuFloat(axisY.z / radius);
        m[10] = rs::render::ToGpuFloat(lightDirection.z / zRange);
        m[11] = 0.0f;
        m[12] = rs::render::ToGpuFloat(-centerX / radius);
        m[13] = rs::render::ToGpuFloat(-centerY / radius);
        m[14] = rs::render::ToGpuFloat(-zNear / zRange);
        m[15] = 1.0f;
        cascade.radius = rs::render::ToGpuFloat(radius);
        cascade.texelWorldSize = rs::render::ToGpuFloat(snappedTexel);
        cascade.snapX =
            static_cast<std::int64_t>(std::floor(Dot(sphereCenter, axisX) / snappedTexel));
        cascade.snapY =
            static_cast<std::int64_t>(std::floor(Dot(sphereCenter, axisY) / snappedTexel));
        cascade.farSplit = rs::render::ToGpuFloat(rangeFar);
        rangeNear = rangeFar;
    }
    output.count = input.cascadeCount;
    return output;
}

} // namespace rs::render

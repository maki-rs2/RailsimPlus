//
// VolumetricFogCommonShader.h - ボリュメトリックフォグの格子を作る側と引く側が共用する HLSL 関数
//
// 奥行きの割り方は VolumetricFogMath.h と同じ式(変えるときは両方)。AtmosphereShader.h の定数部(kPi)の後ろへ連結する。
//
#pragma once

namespace rs::render {

inline constexpr const char* kVolumetricFogCommonShader = R"HLSL(
static const float kVolumetricFogSliceNearScaleM = 4.0; // VolumetricFogMath.h の kSliceNearScaleM

float VolumetricFogSliceDepth(float unit, float maxDistance) {
    return kVolumetricFogSliceNearScaleM * (exp(log(maxDistance / kVolumetricFogSliceNearScaleM + 1.0) * unit) - 1.0);
}

float VolumetricFogSliceUnit(float depth, float maxDistance) {
    return saturate(log(max(depth, 0.0) / kVolumetricFogSliceNearScaleM + 1.0) /
                    log(maxDistance / kVolumetricFogSliceNearScaleM + 1.0));
}

// Henyey-Greenstein の位相関数。cosTheta は視線の向きと太陽へ向かう向きの内積。
float VolumetricFogPhase(float cosTheta, float g) {
    float denominator = max(1.0 + g * g - 2.0 * g * cosTheta, 1.0e-4);
    return (1.0 - g * g) / (4.0 * kPi * denominator * sqrt(denominator));
}
)HLSL";

} // namespace rs::render

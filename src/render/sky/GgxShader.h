//
// GgxShader.h - GGX(Trowbridge-Reitz)の法線分布 D の HLSL 関数
//
// メッシュの直射光の鏡面(MeshBrdfShader.h)と、IBLの畳み込みのサンプルごとのミップ選択(IblShader.h)が
// 同じ式を使う。kPi は AtmosphereShader.h の定数部が定義する(その後ろに連結する)。
//
#pragma once

namespace rs::render {

// D(ndh, alpha) = alpha^2 / (π ((ndh^2 (alpha^2 - 1) + 1)^2))。alpha = roughness^2。
// 分母の下限は 0 割りを避けるだけの値にする(大きいと、つるつるの面のハイライトの山が削れる)。
inline constexpr const char* kGgxDistributionShader = R"HLSL(
static const float kGgxDenominatorMin = 1e-12;
float DistributionGgxAlpha(float ndh, float alpha) {
    float a2 = alpha * alpha;
    float d = ndh * ndh * (a2 - 1.0) + 1.0;
    return a2 / max(kPi * d * d, kGgxDenominatorMin);
}
)HLSL";

} // namespace rs::render

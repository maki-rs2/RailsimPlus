//
// AerialPerspectiveShader.h - 大気遠近(aerial perspective。AP)のLUTを作るHLSLシェーダ文字列
//
// 共通部(BuildAtmosphereCommonShader)の後ろへ連結して使う。LUTは、距離方向のスライスを横に並べた
// 1枚の2Dテクスチャ(幅 = スライス数 × タイル、高さ = タイル)で、全面を1回の描画で作る。
// 画素の x 座標からスライス番号とタイル内の位置を割り出し、そのスライスの距離までの区間で散乱された光(rgb)と
// 透過率のRGB平均(a)を書く。
// 割り当ての式(テクセルの添字→位置、奥行きの割り、視線方向)は AtmosphereMath.cpp の
// AerialUnitFromTexelIndex・AerialDistanceKmFromUnit・AerialViewDirection と同じ(変えるときは両方)。
// 距離の単位はkm。レジスタ: t0=透過率LUT、t1=多重散乱LUT、b0=カメラと太陽(AerialConstants)。
//
#pragma once

#include <format>
#include <string>

#include "../resource/RenderTuning.h"

namespace rs::render {

// LUTの大きさ・最大距離をHLSLの定数へ展開した文字列。
inline std::string BuildAerialPerspectiveConstantsShader(const RenderTuning& tuning) {
    return std::format(
        "static const float kApSliceCount = {};\n"
        "static const float kApTileSize = {};\n"
        "static const float kApMaxDistance = {:.9g};\n",
        tuning.aerialPerspectiveSliceCount, tuning.aerialPerspectiveTileSize,
        tuning.aerialPerspectiveMaxDistanceKm);
}

inline constexpr const char* kAerialPerspectivePassShader = R"HLSL(
cbuffer AerialConstants : register(b0) {
    float3 gCameraRight;   float gTanHalfFovX;
    float3 gCameraUp;      float gTanHalfFovY;
    float3 gCameraForward; float gObserverHeightKm;
    float3 gSunToSun;      float gApPadding2;
};

// テクセルの添字(0〜count-1)が表す位置 0..1。端のテクセルが0と1に当たる。
float ApUnitFromTexelIndex(float index, float count) { return UnitFromSubUv((index + 0.5) / count, count); }

float4 AerialPS(VSOut i) : SV_TARGET {
    float slice = floor(i.pos.x / kApTileSize);
    float texelX = floor(i.pos.x - slice * kApTileSize);
    float texelY = floor(i.pos.y);
    float unitX = ApUnitFromTexelIndex(texelX, kApTileSize);
    float unitY = ApUnitFromTexelIndex(texelY, kApTileSize);
    float unitZ = ApUnitFromTexelIndex(slice, kApSliceCount);

    // 手前を細かく割る(距離 = 最大距離 × 位置の2乗)。距離0は何も散乱していない。
    float distanceKm = kApMaxDistance * unitZ * unitZ;
    if (distanceKm <= 0.0) return float4(0.0, 0.0, 0.0, 1.0);

    float3 dir = normalize(gCameraForward +
                           (2.0 * unitX - 1.0) * gTanHalfFovX * gCameraRight -
                           (2.0 * unitY - 1.0) * gTanHalfFovY * gCameraUp);
    // 観測者の高さはカメラの高さに追従する(AtmosphereMath の ObserverHeightKmFromCameraMeters)。
    // ワールドはY-upで、地球の中心は真下。
    float3 pos = float3(0.0, kBottomRadius + gObserverHeightKm, 0.0);

    // 地面の反射は足さない。サンプルは手前に密に置き、数も区間の長さに応じて増える(Sky-Viewと同じ配置。
    // 一様な配置だと、濃い低空を出ていく視線の透過率が粗くなる)。
    float3 ratio;
    float3 throughput;
    float3 L = IntegrateScatteredLuminanceUntil(pos, dir, gSunToSun, kSunIrradiance, false, kSkyViewMaxSteps, true,
                                                true, distanceKm, ratio, throughput);
    return float4(L, dot(throughput, float3(1.0, 1.0, 1.0)) / 3.0);
}
)HLSL";

} // namespace rs::render

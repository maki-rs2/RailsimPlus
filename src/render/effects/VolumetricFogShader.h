//
// VolumetricFogShader.h - ボリュメトリックフォグの注入と積分の compute シェーダ
//
// 注入: セルごとに、媒質の密度と、そこで視線の向きへ散乱される光を求める(rgb=散乱の強さ、a=消散係数)。
// 積分: カメラから奥へ足し込む(rgb=カメラからそのスライスの中心までの散乱光、a=透過率)。
//
// 式の対応(変えるときは両方):
//   奥行きの割り方・積分  VolumetricFogMath.h
//   密度                  MeshFogShader.h の高さフォグ・HeightFogMath.h
//   定数の並び            VolumetricFogPass.cpp の VolumetricFogConstants
// 注入はセルの中のずらした位置で標本を取り、前フレームの注入の結果と混ぜる(ジッタと履歴でノイズと段をならす)。
// 単位はメッシュのシェーダと同じ。太陽の放射照度は π × 太陽色、環境光は拡散キューブの値(放射照度 ÷ π)。
//
#pragma once

#include <string>

#include "../shadow/ShadowShader.h"
#include "../sky/AtmosphereShader.h"
#include "../sky/ExposureShader.h"
#include "../sky/SunColorShader.h"
#include "VolumetricFogCommonShader.h"

namespace rs::render {

inline constexpr const char* kVolumetricFogConstantsShader = R"HLSL(
cbuffer VolumetricFogConstants : register(b0) {
    float4 gCameraAndDistance; // xyz=カメラ位置 w=格子の奥行きの範囲[m]
    float4 gRight;             // xyz=カメラの右 w=tanHalfFovX
    float4 gUp;                // xyz=カメラの上 w=tanHalfFovY
    float4 gForward;           // xyz=カメラの前 w=位相関数の偏り
    float4 gToSun;             // xyz=太陽へ向かう単位ベクトル w=太陽の光の倍率
    float4 gFog;               // x=基準の高さの密度 y=密度が1/eになる高さ z=基準の高さ w=霧の反射率
    float gAmbientScale;       // 拡散キューブに掛ける倍率
    uint3 gGrid;               // 格子の大きさ
    uint4 gAdaptation;         // x=暗順応の結果のSRV絶対番号 y=標準の空の暗順応が有効か zw=未使用
    uint gDiffuseCubeIdx;
    uint gTransmittanceIdx;
    uint gScatterSrvIdx;
    uint gScatterUavIdx;
    uint gIntegratedUavIdx;
    uint gHistorySrvIdx;       // 前フレームの注入の結果
    float gHistoryWeight;      // 履歴を混ぜる割合(0=履歴なし)
    float gPadding;
    float4 gJitter;            // xyz=セルの中のずれ[セル]
    row_major float4x4 gPrevViewProj; // 前フレームの viewProj(ジッタなし)
};
SamplerState gLinearSmp : register(s1);
)HLSL";

inline constexpr const char* kVolumetricFogBodyShader = R"HLSL(
static const float kSmallExtinction = 1.0e-6;   // VolumetricFogMath.h の kSmallExtinction
static const float kFogExponentLimit = 30.0;    // HeightFogMath.h の kExponentLimit

float SliceDepth(float unit) { return VolumetricFogSliceDepth(unit, gCameraAndDistance.w); }

// カメラ前方向の距離1あたりの視線。xy はセルの画面上の位置(0..1。y は上から下)。
float3 CellRay(float2 unitXy) {
    float x = (unitXy.x * 2.0 - 1.0) * gRight.w;
    float y = (1.0 - unitXy.y * 2.0) * gUp.w;
    return gForward.xyz + gRight.xyz * x + gUp.xyz * y;
}

float FogDensity(float height) {
    float exponent = clamp((height - gFog.z) / gFog.y, -kFogExponentLimit, kFogExponentLimit);
    return gFog.x * exp(-exponent);
}

// セルから太陽が見える割合(1=日なた)。セルには面が無いので、1タップで引き、法線のずらしは要らない。
// 影の距離の外と、シャドウマップの範囲の外は日なた。
float SunVisibility(float3 wpos, float viewDepth) {
    float distanceFade = saturate((gShadowParams2.x - length(wpos - gCameraAndDistance.xyz)) /
                                  max(gShadowParams2.x - gShadowParams2.y, 1.0e-3));
    if (distanceFade <= 0.0) return 1.0;
    uint cascade = ShadowCascadeIndex(viewDepth);
    float4 clip = mul(float4(wpos, 1.0), gShadowViewProj[cascade]);
    if (clip.z < 0.0 || clip.z > 1.0) return 1.0;
    float2 uv = float2(clip.x * 0.5 + 0.5, 0.5 - clip.y * 0.5);
    if (any(uv < 0.0) || any(uv > 1.0)) return 1.0;
    Texture2DArray<float> shadowMap = ResourceDescriptorHeap[gShadowIndex.x];
    float visibility = shadowMap.SampleCmpLevelZero(gShadowSmp, float3(uv, cascade), clip.z);
    return 1.0 - (1.0 - visibility) * distanceFade;
}

[numthreads(4, 4, 4)]
void CSInject(uint3 id : SV_DispatchThreadID) {
    if (any(id >= gGrid)) return;
    RWTexture3D<float4> output = ResourceDescriptorHeap[gScatterUavIdx];
    float3 unit = (float3(id) + 0.5 + gJitter.xyz) / float3(gGrid);
    float viewDepth = SliceDepth(unit.z);
    float3 ray = CellRay(unit.xy);
    float3 wpos = gCameraAndDistance.xyz + ray * viewDepth;
    float density = FogDensity(wpos.y);

    TextureCube<float4> diffuseCube = ResourceDescriptorHeap[gDiffuseCubeIdx];
    float3 ambient = gAmbientScale * diffuseCube.SampleLevel(gLinearSmp, float3(0.0, 1.0, 0.0), 0).rgb *
                     IblExposureCorrection();

    Texture2D<float4> transmittance = ResourceDescriptorHeap[gTransmittanceIdx];
    float3 sunColor = SunColorFromTransmittance(transmittance, gLinearSmp, gToSun.xyz) * AdaptationGainFromTexture();
    float cosTheta = dot(normalize(ray), gToSun.xyz);
    float3 sun = kPi * sunColor * VolumetricFogPhase(cosTheta, gForward.w) * SunVisibility(wpos, viewDepth) * gToSun.w;

    float4 current = float4(density * gFog.w * (ambient + sun), density);

    // 同じ世界の位置を前フレームの格子で引く。格子の外なら今フレームだけを使う。
    float weight = 0.0;
    float4 history = float4(0.0, 0.0, 0.0, 0.0);
    if (gHistoryWeight > 0.0) {
        float4 prevClip = mul(float4(wpos, 1.0), gPrevViewProj);
        if (prevClip.w > 0.0) {
            float2 prevNdc = prevClip.xy / prevClip.w;
            float3 prevUnit = float3(prevNdc.x * 0.5 + 0.5, 0.5 - prevNdc.y * 0.5,
                                     VolumetricFogSliceUnit(prevClip.w, gCameraAndDistance.w));
            if (all(prevUnit.xy >= 0.0) && all(prevUnit.xy <= 1.0) && prevClip.w <= gCameraAndDistance.w) {
                Texture3D<float4> previous = ResourceDescriptorHeap[gHistorySrvIdx];
                history = previous.SampleLevel(gLinearSmp, prevUnit, 0);
                weight = gHistoryWeight;
            }
        }
    }
    output[id] = lerp(current, history, weight);
}

float ScatterIntegral(float extinction, float length) {
    if (extinction < kSmallExtinction) return length;
    return (1.0 - exp(-extinction * length)) / extinction;
}

[numthreads(8, 8, 1)]
void CSIntegrate(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= gGrid.xy)) return;
    Texture3D<float4> scatter = ResourceDescriptorHeap[gScatterSrvIdx];
    RWTexture3D<float4> output = ResourceDescriptorHeap[gIntegratedUavIdx];
    float2 unitXy = (float2(id.xy) + 0.5) / float2(gGrid.xy);
    float rayLength = length(CellRay(unitXy));
    float3 accumulated = float3(0.0, 0.0, 0.0);
    float accumulatedTransmittance = 1.0;
    float sliceCount = float(gGrid.z);
    for (uint z = 0u; z < gGrid.z; ++z) {
        float4 cell = scatter.Load(int4(id.xy, z, 0));
        float start = SliceDepth(float(z) / sliceCount);
        float center = SliceDepth((float(z) + 0.5) / sliceCount);
        float end = SliceDepth((float(z) + 1.0) / sliceCount);
        // 引く側はテクセルの中心を線形に補間するので、スライスの中心までの値を書く。
        float halfLength = (center - start) * rayLength;
        output[uint3(id.xy, z)] =
            float4(accumulated + accumulatedTransmittance * cell.rgb * ScatterIntegral(cell.a, halfLength),
                   accumulatedTransmittance * exp(-cell.a * halfLength));
        float fullLength = (end - start) * rayLength;
        accumulated += accumulatedTransmittance * cell.rgb * ScatterIntegral(cell.a, fullLength);
        accumulatedTransmittance *= exp(-cell.a * fullLength);
    }
}
)HLSL";

inline std::string BuildVolumetricFogShader() {
    return BuildAtmosphereConstantsShader() + kTransmittanceUvShader + kSunColorShader + kVolumetricFogConstantsShader +
           kShadowConstantsShader + kExposureShader + kVolumetricFogCommonShader + kVolumetricFogBodyShader;
}

} // namespace rs::render

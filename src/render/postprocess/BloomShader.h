//
// BloomShader.h - ブルーム(明るい部分の切り出し+縮小チェーン+足し戻し)用 HLSL シェーダ文字列
//
// 縮小は13タップのフィルタ(CoD:AW方式)。最初の1段だけ、明るい部分の切り出し(しきい値・soft knee。Unity と同じ式)と、
// 輝度の重みを付けた平均(Karis平均)を行う。
// 拡大は3x3のテント。ブレンドは固定機能で、係数 f は OMSetBlendFactor で渡す。
//   段の間: `拡大結果 * f + 出力先 * (1 - f)`(SrcBlend=BLEND_FACTOR, DestBlend=INV_BLEND_FACTOR)
//   最終  : `出力先 + 拡大結果 * f`(SrcBlend=BLEND_FACTOR, DestBlend=ONE)
// 縮小・拡大とも各フィルタの重みの合計は1。
//
#pragma once

#include <string>

#include "../pipeline/ShaderColor.h"

namespace rs::render {

inline const std::string kBloomShader = std::string(kColorConstantsShader) + R"(
Texture2D<float4> gSrc : register(t0);
SamplerState gLinearClamp : register(s0);

// invDst: 出力先の1画素のUV幅(画素の中心から読む位置を求める)。invSrc: 入力の1画素のUV幅(タップのずらし幅)。
// exposure: Karis平均の重みを求める輝度に掛ける倍率(トーンマップの露出。重みを露出後の明るさで決めるため)。
// karis: 0以外なら、縮小の最初の段(明るい部分の切り出しとKaris平均、NaN・極端な値の除去を行う)。
// threshold: 切り出しのしきい値(露出後の明るさ。画素の最大チャンネルがこれを超えた分だけ残す)。
cbuffer BloomConstants : register(b0) {
    float2 gInvDst;
    float2 gInvSrc;
    float gExposure;
    uint gKaris;
    float gThreshold;
};

struct VSOut { float4 pos : SV_POSITION; };

VSOut VSMain(uint vid : SV_VertexID) {
    VSOut o;
    float2 p = float2((vid << 1) & 2, vid & 2); // (0,0),(2,0),(0,2)
    o.pos = float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}

static const float kHalfMax = 60000.0; // fp16の上限(65504)に収まる値

// 明るい部分だけを残す。しきい値の手前 kSoftKnee の幅は2次の曲線でなめらかにつなぐ(段が出ない)。色の比は変えない。
static const float kSoftKnee = 0.5;

float3 ApplyThreshold(float3 c) {
    float brightness = max(c.r, max(c.g, c.b)) * gExposure;
    float knee = gThreshold * kSoftKnee;
    float soft = clamp(brightness - gThreshold + knee, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 1e-5);
    float contribution = max(soft, brightness - gThreshold) / max(brightness, 1e-5);
    return c * contribution;
}

float3 Tap(float2 uv, float2 offsetTexels) {
    float3 c = gSrc.SampleLevel(gLinearClamp, uv + offsetTexels * gInvSrc, 0).rgb;
    if (gKaris != 0) {
        // NaN・無限大・負は0にし、極端に大きい値は上限で止める(1画素の異常値が画面全体へ広がるのを防ぐ)。
        c = isnan(c) ? 0.0 : clamp(c, 0.0, kHalfMax);
        c = ApplyThreshold(c);
    }
    return c;
}

float KarisWeight(float3 c) {
    float luma = dot(c * gExposure, kRec709LuminanceWeights);
    return 1.0 / (1.0 + luma);
}

float4 PSDown(VSOut i) : SV_TARGET {
    float2 uv = i.pos.xy * gInvDst;
    float3 a = Tap(uv, float2(-2, -2));
    float3 b = Tap(uv, float2( 0, -2));
    float3 c = Tap(uv, float2( 2, -2));
    float3 d = Tap(uv, float2(-2,  0));
    float3 e = Tap(uv, float2( 0,  0));
    float3 f = Tap(uv, float2( 2,  0));
    float3 g = Tap(uv, float2(-2,  2));
    float3 h = Tap(uv, float2( 0,  2));
    float3 k = Tap(uv, float2( 2,  2));
    float3 j = Tap(uv, float2(-1, -1));
    float3 l = Tap(uv, float2( 1, -1));
    float3 m = Tap(uv, float2(-1,  1));
    float3 n = Tap(uv, float2( 1,  1));
    float3 color;
    if (gKaris != 0) {
        // 4つの2x2の塊(重み 1/8 ずつ)と中央の塊(重み 1/2)を、それぞれの輝度で重みを付けて平均する。
        float3 g0 = (a + b + d + e) * 0.25;
        float3 g1 = (b + c + e + f) * 0.25;
        float3 g2 = (d + e + g + h) * 0.25;
        float3 g3 = (e + f + h + k) * 0.25;
        float3 g4 = (j + l + m + n) * 0.25;
        float w0 = 0.125 * KarisWeight(g0);
        float w1 = 0.125 * KarisWeight(g1);
        float w2 = 0.125 * KarisWeight(g2);
        float w3 = 0.125 * KarisWeight(g3);
        float w4 = 0.5 * KarisWeight(g4);
        color = (g0 * w0 + g1 * w1 + g2 * w2 + g3 * w3 + g4 * w4) / (w0 + w1 + w2 + w3 + w4);
    } else {
        color = e * 0.125 + (a + c + g + k) * 0.03125 + (b + d + f + h) * 0.0625 + (j + l + m + n) * 0.125;
    }
    return float4(color, 1.0);
}

float4 PSUp(VSOut i) : SV_TARGET {
    float2 uv = i.pos.xy * gInvDst;
    float3 a = Tap(uv, float2(-1, -1));
    float3 b = Tap(uv, float2( 0, -1));
    float3 c = Tap(uv, float2( 1, -1));
    float3 d = Tap(uv, float2(-1,  0));
    float3 e = Tap(uv, float2( 0,  0));
    float3 f = Tap(uv, float2( 1,  0));
    float3 g = Tap(uv, float2(-1,  1));
    float3 h = Tap(uv, float2( 0,  1));
    float3 k = Tap(uv, float2( 1,  1));
    float3 color = (a + c + g + k + 2.0 * (b + d + f + h) + 4.0 * e) * (1.0 / 16.0);
    return float4(color, 1.0);
}
)";

} // namespace rs::render

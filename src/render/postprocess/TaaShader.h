//
// TaaShader.h - TAA の解決パスの HLSL(DXC、SM6.0。bindless テーブルの配列で SRV を番号から引く)
//
#pragma once

#include <string>

#include "../pipeline/ShaderColor.h"

namespace rs::render {

inline const std::string kTaaShader = std::string(kColorConstantsShader) + R"(
cbuffer TaaConstants : register(b0) {
    row_major float4x4 gInvViewProj;   // 今フレームの(ジッタなしの)viewProj の逆行列
    row_major float4x4 gPrevViewProj;  // 前フレームの(ジッタなしの)viewProj
    float2 gJitter;                    // 今フレームのずれ(NDC。右・上が正)
    float2 gInvSize;                   // 1 / 画面の大きさ
    float2 gSize;                      // 画面の大きさ
    float gHistoryWeight;              // 履歴の重み(静止時)
    float gSharpness;                  // 解決後に足すシャープ(0 で無し)
    uint gColorIdx;                    // 今フレームの HDR の SRV 番号
    uint gHistoryIdx;                  // 履歴の SRV 番号
    uint gDepthIdx;                    // 本編の深度(R32_FLOAT。Reversed-Z)の SRV 番号
    uint gVelocityIdx;                 // 速度(RG16F)の SRV 番号
    uint gReset;                       // 1 なら履歴を使わない
    uint3 gPad;
};
SamplerState gLinear : register(s0);

struct VSOut { float4 pos : SV_POSITION; };

VSOut VSMain(uint id : SV_VertexID) {
    VSOut o;
    float2 p = float2((id << 1) & 2, id & 2);
    o.pos = float4(p * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}

// 異常値(NaN・無限大・負)は 0、極端に大きい値は fp16 の上限の手前で止める。1画素の異常値が履歴に残らないように。
float3 Sanitize(float3 c) {
    c = select(isfinite(c), c, float3(0.0, 0.0, 0.0));
    return clamp(c, 0.0, 60000.0);
}

float Luma(float3 c) { return dot(c, kRec709LuminanceWeights); }
// 明るい画素が平均を支配しないよう、輝度で割って縮めた値(Karis)で比べる。
float3 ToWeighted(float3 c) { return c / (1.0 + Luma(c)); }
float3 FromWeighted(float3 t) { return t / max(1.0 - Luma(t), 1.0e-4); }

float3 ToYCoCg(float3 c) {
    return float3(0.25 * c.r + 0.5 * c.g + 0.25 * c.b, 0.5 * c.r - 0.5 * c.b, -0.25 * c.r + 0.5 * c.g - 0.25 * c.b);
}
float3 FromYCoCg(float3 c) {
    return float3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z);
}

// 範囲の中心から q へ向かう線が範囲の外へ出る点まで、q を引き戻す。
float3 ClipToBox(float3 boxMin, float3 boxMax, float3 q) {
    float3 center = 0.5 * (boxMax + boxMin);
    float3 extent = 0.5 * (boxMax - boxMin) + 1.0e-5;
    float3 v = q - center;
    float3 a = abs(v / extent);
    float m = max(a.x, max(a.y, a.z));
    return m > 1.0 ? center + v / m : q;
}

// Catmull-Rom(5 タップ)で履歴を引く。uv は 0〜1。
float3 SampleHistoryCatmullRom(Texture2D<float4> tex, float2 uv) {
    float2 pos = uv * gSize;
    float2 center = floor(pos - 0.5) + 0.5;
    float2 f = pos - center;
    float2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    float2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    float2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    float2 w3 = f * f * (-0.5 + 0.5 * f);
    float2 w12 = w1 + w2;
    float2 offset12 = w2 / w12;
    float2 uv0 = (center - 1.0) * gInvSize;
    float2 uv3 = (center + 2.0) * gInvSize;
    float2 uv12 = (center + offset12) * gInvSize;
    float3 result = float3(0.0, 0.0, 0.0);
    result += tex.SampleLevel(gLinear, float2(uv12.x, uv0.y), 0).rgb * (w12.x * w0.y);
    result += tex.SampleLevel(gLinear, float2(uv0.x, uv12.y), 0).rgb * (w0.x * w12.y);
    result += tex.SampleLevel(gLinear, float2(uv12.x, uv12.y), 0).rgb * (w12.x * w12.y);
    result += tex.SampleLevel(gLinear, float2(uv3.x, uv12.y), 0).rgb * (w3.x * w12.y);
    result += tex.SampleLevel(gLinear, float2(uv12.x, uv3.y), 0).rgb * (w12.x * w3.y);
    return max(result, 0.0);
}

float2 PixelToNdc(float2 pixelCenter) {
    return float2(pixelCenter.x * gInvSize.x * 2.0 - 1.0, 1.0 - pixelCenter.y * gInvSize.y * 2.0);
}

float4 PSResolve(VSOut input) : SV_Target {
    Texture2D<float4> colorTex = gBindlessTex2D[gColorIdx];
    Texture2D<float4> historyTex = gBindlessTex2D[gHistoryIdx];
    Texture2D<float> depthTex = gBindlessTex2DR[gDepthIdx];
    Texture2D<float2> velocityTex = gBindlessTex2DRg[gVelocityIdx];

    int2 pixel = int2(input.pos.xy);
    int2 maxPixel = int2(gSize) - 1;
    // ずれ[画素。右・下が正]。NDC の y は上が正、画素の y は下が正。
    float2 jitterPixels = float2(gJitter.x * 0.5 * gSize.x, -gJitter.y * 0.5 * gSize.y);

    // 今フレームの 3x3 の近傍。重みを付けた YCoCg の平均・分散・範囲と、最も近い(Reversed-Z で最大の)深度の画素を求める。
    float3 center = float3(0.0, 0.0, 0.0);
    float centerWeight = 0.0;
    float3 sum = float3(0.0, 0.0, 0.0);
    float3 sumSq = float3(0.0, 0.0, 0.0);
    float3 boxMin = float3(1.0e9, 1.0e9, 1.0e9);
    float3 boxMax = float3(-1.0e9, -1.0e9, -1.0e9);
    float closestDepth = -1.0;
    int2 closestPixel = pixel;
    [unroll] for (int dy = -1; dy <= 1; ++dy) {
        [unroll] for (int dx = -1; dx <= 1; ++dx) {
            int2 q = clamp(pixel + int2(dx, dy), int2(0, 0), maxPixel);
            float3 c = Sanitize(colorTex.Load(int3(q, 0)).rgb);
            float3 y = ToYCoCg(ToWeighted(c));
            // 今フレームの標本は、ジッタぶんずれた位置のもの。画素の中心の値を、近い標本ほど重く混ぜて作る
            // (Unreal と同じ Blackman-Harris 型の重み。補正しないと、ずれが毎フレーム変わるぶん、輪郭が揺れる)。
            float2 offset = float2(dx, dy) - jitterPixels;
            float filterWeight = exp(-2.29 * dot(offset, offset));
            center += y * filterWeight;
            centerWeight += filterWeight;
            sum += y;
            sumSq += y * y;
            boxMin = min(boxMin, y);
            boxMax = max(boxMax, y);
            float d = depthTex.Load(int3(q, 0));
            if (d > closestDepth) {
                closestDepth = d;
                closestPixel = q;
            }
        }
    }
    center /= centerWeight;
    float3 mean = sum / 9.0;
    float3 sigma = sqrt(max(sumSq / 9.0 - mean * mean, 0.0));
    float3 clipMin = max(boxMin, mean - 2.0 * sigma);
    float3 clipMax = min(boxMax, mean + 2.0 * sigma);

    // 履歴を引く位置。カメラの動き(深度から世界座標を復元して前フレームの viewProj で射影)に、物体の速度を足す。
    // 位置は今の画素の中心で、深度と速度だけを最も近い画素から取る(位置まで最も近い画素にすると、静止していても
    // 履歴を最大1画素ずらして引き、模様が流れる)。今フレームの標本は画素の中心のものとして扱い、ジッタ分は引かない
    // (引くと、静止した画素でも履歴を毎回小数画素ずらして引き、ぼける)。空(深度 0)は遠方の点になる。
    float2 ndc = PixelToNdc(float2(pixel) + 0.5);
    float4 world = mul(float4(ndc, closestDepth, 1.0), gInvViewProj);
    world.xyz /= world.w;
    float4 prevClip = mul(float4(world.xyz, 1.0), gPrevViewProj);
    float2 prevNdc = prevClip.w > 1.0e-5 ? prevClip.xy / prevClip.w : ndc;
    prevNdc += velocityTex.Load(int3(closestPixel, 0)).xy;
    float2 prevUv = prevNdc * float2(0.5, -0.5) + 0.5;

    bool historyValid = gReset == 0u && all(prevUv > 0.0) && all(prevUv < 1.0);
    float3 resolved = center;
    if (historyValid) {
        float3 history = Sanitize(SampleHistoryCatmullRom(historyTex, prevUv));
        float3 historyY = ClipToBox(clipMin, clipMax, ToYCoCg(ToWeighted(history)));
        float2 motionPixels = (prevUv - (float2(pixel) + 0.5) * gInvSize) * gSize;
        // 動きが大きい画素ほど、履歴の重みを下げる。
        float feedback = gHistoryWeight * lerp(1.0, 0.6, saturate(length(motionPixels) / 16.0));
        resolved = lerp(center, historyY, feedback);
        // シャープ: 解決後の明るさ(Y)から、今フレームの 3x3 の平均を引いた差を足す。近傍の範囲を超えないようにする。
        resolved.x = clamp(resolved.x + gSharpness * (resolved.x - mean.x), boxMin.x, boxMax.x);
    }
    float3 color = FromWeighted(FromYCoCg(resolved));
    return float4(Sanitize(color), 1.0);
}
)";

} // namespace rs::render

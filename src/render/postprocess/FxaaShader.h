//
// FxaaShader.h - FXAA(トーンマップ後のLDRに掛ける輪郭のアンチエイリアス)用 HLSL シェーダ文字列
//
// 入力は R8G8B8A8_UNORM_SRGB のLDR(サンプルするとリニア値)。輝度はガンマ空間の見た目に合わせて
// リニア輝度の平方根で取る。出力はリニア値で、sRGBへの変換は出力先のRTV(_SRGB形式)が行う。
// 輪郭でない画素は、入力の画素をそのまま返す。
//
#pragma once

#include <string>

#include "../pipeline/ShaderColor.h"

namespace rs::render {

inline const std::string kFxaaShader = std::string(kColorConstantsShader) + R"(
Texture2D<float4> gLdr : register(t0);
SamplerState gLinearClamp : register(s0);

cbuffer FxaaConstants : register(b0) {
    float2 gInvSize;    // 1 / 入力の大きさ(画素)
    float2 gOutOrigin;  // 出力先の中の矩形の左上(画素)
    float2 gOutSize;    // 矩形の大きさ(画素)。0以下なら出力先の全体
};

// 輪郭とみなす輝度差の下限(絶対値)と、周囲の最大輝度に対する比。
static const float kContrastMin = 0.0312;
static const float kContrastRelative = 0.125;
// 輪郭でない画素へのサブピクセル混ぜの強さ(0..1)。
static const float kSubpixelBlend = 0.75;
// 輪郭に沿った探索の刻み(画素単位)。最後の刻みで打ち切り、端までの距離を見積もる。
static const int kEdgeSteps = 6;
static const float kEdgeStepSize[kEdgeSteps] = {1.0, 1.5, 2.0, 2.0, 2.0, 4.0};
static const float kEdgeStepGuess = 8.0;

struct VSOut { float4 pos : SV_POSITION; };

VSOut VSMain(uint vid : SV_VertexID) {
    VSOut o;
    float2 p = float2((vid << 1) & 2, vid & 2); // (0,0),(2,0),(0,2)
    o.pos = float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}

float3 SampleColor(float2 uv) { return gLdr.SampleLevel(gLinearClamp, uv, 0).rgb; }
float LumaOf(float3 c) { return sqrt(max(dot(c, kRec601LuminanceWeights), 0.0)); }
float SampleLuma(float2 uv) { return LumaOf(SampleColor(uv)); }

float4 PSMain(VSOut i) : SV_TARGET {
    // 矩形へ出すときは、矩形の中の位置を入力の全体(0..1)へ引き伸ばして読む。
    float2 uv = gOutSize.x > 0.0 ? (i.pos.xy - gOutOrigin) / gOutSize : i.pos.xy * gInvSize;
    float3 mColor = SampleColor(uv);
    float m = LumaOf(mColor);
    float n = SampleLuma(uv + float2(0, -gInvSize.y));
    float s = SampleLuma(uv + float2(0, gInvSize.y));
    float e = SampleLuma(uv + float2(gInvSize.x, 0));
    float w = SampleLuma(uv + float2(-gInvSize.x, 0));
    float highest = max(max(max(max(m, n), e), s), w);
    float lowest = min(min(min(min(m, n), e), s), w);
    float contrast = highest - lowest;
    if (contrast < max(kContrastMin, kContrastRelative * highest)) return float4(mColor, 1.0);

    float ne = SampleLuma(uv + float2(gInvSize.x, -gInvSize.y));
    float nw = SampleLuma(uv + float2(-gInvSize.x, -gInvSize.y));
    float se = SampleLuma(uv + float2(gInvSize.x, gInvSize.y));
    float sw = SampleLuma(uv + float2(-gInvSize.x, gInvSize.y));

    // 輪郭の向きに関係なく、周囲との差から出すサブピクセルの混ぜ具合。
    float neighborAverage = (2.0 * (n + e + s + w) + ne + nw + se + sw) / 12.0;
    float subpixel = saturate(abs(neighborAverage - m) / contrast);
    subpixel = smoothstep(0.0, 1.0, subpixel);
    subpixel = subpixel * subpixel * kSubpixelBlend;

    // 輪郭の向き(水平なら上下の差が大きい)。
    float horizontal = abs(n + s - 2.0 * m) * 2.0 + abs(ne + se - 2.0 * e) + abs(nw + sw - 2.0 * w);
    float vertical = abs(e + w - 2.0 * m) * 2.0 + abs(ne + nw - 2.0 * n) + abs(se + sw - 2.0 * s);
    bool isHorizontal = horizontal >= vertical;

    // 輪郭をはさむ2辺のうち、差が大きい側へ向かう。p は uv が増える向き(下・右)、n は減る向き(上・左)。
    // n・s は画面の上・下(上が uv.y の小さい側)なので、下の辺 s が p 側になる。
    float pLuma = isHorizontal ? s : e;
    float nLuma = isHorizontal ? n : w;
    float pGradient = abs(pLuma - m);
    float nGradient = abs(nLuma - m);
    float pixelStep = isHorizontal ? gInvSize.y : gInvSize.x;
    float oppositeLuma;
    float gradient;
    if (pGradient < nGradient) {
        pixelStep = -pixelStep;
        oppositeLuma = nLuma;
        gradient = nGradient;
    } else {
        oppositeLuma = pLuma;
        gradient = pGradient;
    }

    // 輪郭の線(画素の境界)に沿って両方向へ探索し、輪郭の端を探す。
    float2 edgeUv = uv;
    float2 edgeStep;
    if (isHorizontal) {
        edgeUv.y += pixelStep * 0.5;
        edgeStep = float2(gInvSize.x, 0);
    } else {
        edgeUv.x += pixelStep * 0.5;
        edgeStep = float2(0, gInvSize.y);
    }
    float edgeLuma = 0.5 * (m + oppositeLuma);
    float gradientThreshold = 0.25 * gradient;

    float2 pUv = edgeUv + edgeStep;
    float pLumaDelta = SampleLuma(pUv) - edgeLuma;
    bool pAtEnd = abs(pLumaDelta) >= gradientThreshold;
    [unroll]
    for (int a = 0; a < kEdgeSteps; ++a) {
        if (!pAtEnd) {
            pUv += edgeStep * kEdgeStepSize[a];
            pLumaDelta = SampleLuma(pUv) - edgeLuma;
            pAtEnd = abs(pLumaDelta) >= gradientThreshold;
        }
    }
    if (!pAtEnd) pUv += edgeStep * kEdgeStepGuess;

    float2 nUv = edgeUv - edgeStep;
    float nLumaDelta = SampleLuma(nUv) - edgeLuma;
    bool nAtEnd = abs(nLumaDelta) >= gradientThreshold;
    [unroll]
    for (int b = 0; b < kEdgeSteps; ++b) {
        if (!nAtEnd) {
            nUv -= edgeStep * kEdgeStepSize[b];
            nLumaDelta = SampleLuma(nUv) - edgeLuma;
            nAtEnd = abs(nLumaDelta) >= gradientThreshold;
        }
    }
    if (!nAtEnd) nUv -= edgeStep * kEdgeStepGuess;

    // 近い端までの距離から、輪郭の線を横切る向きの混ぜ具合を出す。端の向こうで輝度が輪郭の反対側に
    // 戻らないとき(線の途中の画素)は混ぜない。
    float pDistance = isHorizontal ? pUv.x - uv.x : pUv.y - uv.y;
    float nDistance = isHorizontal ? uv.x - nUv.x : uv.y - nUv.y;
    float shortestDistance;
    bool deltaSign;
    if (pDistance <= nDistance) {
        shortestDistance = pDistance;
        deltaSign = pLumaDelta >= 0.0;
    } else {
        shortestDistance = nDistance;
        deltaSign = nLumaDelta >= 0.0;
    }
    float edgeBlend = 0.0;
    if (deltaSign != (m - edgeLuma >= 0.0)) edgeBlend = 0.5 - shortestDistance / (pDistance + nDistance);

    float finalBlend = max(subpixel, edgeBlend);
    float2 finalUv = uv;
    if (isHorizontal) finalUv.y += pixelStep * finalBlend;
    else finalUv.x += pixelStep * finalBlend;
    return float4(SampleColor(finalUv), 1.0);
}
)";

} // namespace rs::render

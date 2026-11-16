//
// TonemapShader.h - トーンマップ(HDR→バックバッファ)用 HLSL シェーダ文字列
//
// CPU の基準実装と係数を共有し、曲線の差異を防ぐ。
// 出力はリニア値で、sRGBへの変換は出力先のRTV(_SRGB形式)が行う。
//
#pragma once

#include "TonemapCoefficients.h"

namespace rs::render {

inline const std::string kTonemapShader = BuildTonemapCoefficientsShader() + R"(
Texture2D<float4> gHdr : register(t0);

// mode: TonemapMath.h の TonemapMode と同じ番号。
// 0=ACES, 1=パススルー(saturateのみ), 2=Hable(RailSim II -k-build), 3=Reinhard, 4=PBR Neutral, 5=AgX, 6=triple section, 7=ACES(Hill版フィット)
cbuffer TonemapConstants : register(b0) {
    float gExposure;
    uint gMode;
    float2 gSrcSize;  // 入力の大きさ(画素)
    float4 gOutRect;  // 出力先の中の矩形(x, y, 幅, 高さ)。幅が0以下なら出力先の全体
};

struct VSOut { float4 pos : SV_POSITION; };

VSOut VSMain(uint vid : SV_VertexID) {
    VSOut o;
    float2 p = float2((vid << 1) & 2, vid & 2); // (0,0),(2,0),(0,2)
    o.pos = float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}

float3 AcesNarkowicz(float3 x) {
    return (x * (kAcesA * x + kAcesB)) / (x * (kAcesC * x + kAcesD) + kAcesE);
}

float3 HableK(float3 x) {
    const float a = kHableA, b = kHableB, c = kHableC, d = kHableD, e = kHableE, f = kHableF;
    return ((x * (a * x + c * b) + d * e) / (x * (a * x + b) + d * f) - e / f) * kHableScale;
}

float3 Reinhard(float3 x) {
    return x / (1.0 + x);
}

float3 PbrNeutral(float3 color) {
    const float startCompression = kNeutralStartCompression;
    const float desaturation = kNeutralDesaturation;
    float x = min(color.r, min(color.g, color.b));
    float offset = x < kNeutralToeEnd ? x - x * x / (4.0 * kNeutralOffset) : kNeutralOffset;
    color -= offset;
    float peak = max(color.r, max(color.g, color.b));
    if (peak < startCompression) return color;
    const float d = 1.0 - startCompression;
    float newPeak = 1.0 - d * d / (peak + d - startCompression);
    color *= newPeak / peak;
    float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
    return lerp(color, newPeak.xxx, g);
}

float3 Agx(float3 color) {
    const float minEv = kAgxMinEv;
    const float maxEv = kAgxMaxEv;
    float3 x = mul(kAgxInset, color);
    float3 ev = x > 0.0 ? log2(max(x, 1e-30)) : minEv;
    float3 t = (clamp(ev, minEv, maxEv) - minEv) / (maxEv - minEv);
    float3 t2 = t * t;
    float3 t4 = t2 * t2;
    t = kAgxPolynomial0 * t4 * t2 + kAgxPolynomial1 * t4 * t + kAgxPolynomial2 * t4 +
        kAgxPolynomial3 * t2 * t + kAgxPolynomial4 * t2 + kAgxPolynomial5 * t + kAgxPolynomial6;
    return pow(max(mul(kAgxOutset, t), 0.0), kAgxDisplayGamma);
}

float3 TripleSection(float3 x) {
    const float p = kTripleSectionMaxBrightness, a = kTripleSectionContrast, m = kTripleSectionLinearStart;
    const float l = kTripleSectionLinearLength, c = kTripleSectionBlackTightness, b = kTripleSectionPedestal;
    float l0 = ((p - m) * l) / a;
    float s0 = m + l0;
    float s1 = m + a * l0;
    float c2 = (a * p) / (p - s1);
    float cp = -c2 / p;
    float3 w0 = 1.0 - smoothstep(0.0, m, x);
    float3 w2 = x >= s0 ? 1.0 : 0.0;
    float3 w1 = 1.0 - w0 - w2;
    float3 toe = m * pow(x / m, c) + b;
    float3 shoulder = p - (p - s1) * exp(cp * (x - s0));
    float3 linearPart = m + a * (x - m);
    return toe * w0 + linearPart * w1 + shoulder * w2;
}

// 入力の露出補正は、リニアsRGB→AP1→RRT+ODT→リニアsRGBのフィットの前提。
float3 AcesFitted(float3 color) {
    float3 v = mul(kAcesFittedInput, color / kAcesFittedExposureScale);
    float3 a = v * (v + kAcesFittedA0) - kAcesFittedA1;
    float3 b = v * (kAcesFittedB0 * v + kAcesFittedB1) + kAcesFittedB2;
    return mul(kAcesFittedOutput, a / b);
}

float4 PSMain(VSOut i) : SV_TARGET {
    // 矩形へ出すときは、矩形の中の位置を入力の全体へ引き伸ばして読む。
    float2 srcPos = gOutRect.z > 0.0 ? (i.pos.xy - gOutRect.xy) / gOutRect.zw * gSrcSize : i.pos.xy;
    float3 hdr = gHdr.Load(int3(int2(min(srcPos, gSrcSize - 1.0)), 0)).rgb;
    if (gMode == 1) return float4(saturate(hdr), 1.0);
    float3 x = max(hdr * gExposure, 0.0);
    float3 ldr;
    if (gMode == 2) ldr = HableK(x);
    else if (gMode == 3) ldr = Reinhard(x);
    else if (gMode == 4) ldr = PbrNeutral(x);
    else if (gMode == 5) ldr = Agx(x);
    else if (gMode == 6) ldr = TripleSection(x);
    else if (gMode == 7) ldr = AcesFitted(x);
    else ldr = AcesNarkowicz(x);
    return float4(saturate(ldr), 1.0);
}
)";

} // namespace rs::render

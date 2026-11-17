//
// LuminanceMeterShader.h - 測光パスの HLSL(DXC、SM6.0。bindless テーブルの配列で SRV を番号から引く)
//
// 出力の1画素が、入力のその区画の「輝度の対数(log2)の平均」になる。区画の中を 8x8 の点で拾って平均する。
// 輝度は Rec.709 の係数で求め、異常値(NaN・無限大)は 0、極端な値は [最小, 最大] に収めてから対数にする
// (太陽や光源の飛び抜けた値で平均が引きずられないようにする)。
//
#pragma once

#include <string>

namespace rs::render {

inline const std::string kLuminanceMeterShader = R"(
cbuffer LuminanceMeterConstants : register(b0) {
    float2 gInputSize;     // 入力の大きさ[画素]
    float2 gGridSize;      // 出力の格子の大きさ
    uint gColorIdx;        // 入力(シーンの HDR)の SRV 番号
    float gMinLuminance;   // 1サンプルの輝度の下限
    float gMaxLuminance;   // 1サンプルの輝度の上限
    uint gPad0;
};

struct VSOut { float4 pos : SV_POSITION; };

VSOut VSMain(uint id : SV_VertexID) {
    VSOut o;
    float2 p = float2((id << 1) & 2, id & 2);
    o.pos = float4(p * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}

static const uint kTapsPerSide = 8;

float PSMeter(VSOut input) : SV_Target {
    Texture2D<float4> colorTex = gBindlessTex2D[gColorIdx];
    float2 blockSize = gInputSize / gGridSize;
    float2 origin = floor(input.pos.xy) * blockSize;
    int2 maxPixel = int2(gInputSize) - 1;
    float sum = 0.0;
    [unroll] for (uint j = 0; j < kTapsPerSide; ++j) {
        [unroll] for (uint i = 0; i < kTapsPerSide; ++i) {
            float2 p = origin + (float2(i, j) + 0.5) / float(kTapsPerSide) * blockSize;
            float3 c = colorTex.Load(int3(min(int2(p), maxPixel), 0)).rgb;
            c = select(isfinite(c), c, float3(0.0, 0.0, 0.0));
            float luminance = dot(c, float3(0.2126, 0.7152, 0.0722));
            sum += log2(clamp(luminance, gMinLuminance, gMaxLuminance));
        }
    }
    return sum / float(kTapsPerSide * kTapsPerSide);
}
)";

} // namespace rs::render

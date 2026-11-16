//
// SkyBodyShader.h - プラグインの空の Sun.x・Moon.x を描くHLSL(位置+UV、テクスチャ×材質色)
//
#pragma once

namespace rs::render {

// b0=ルート定数(20 DWORD: ローカル→クリップの行列16 + 材質色4)、t0=テクスチャ、s0=リニアクランプ。
// 出力はリニア(テクスチャはsRGBビューで引くのでリニアになっている)。アルファはブレンドのSrcAlphaに使う。
inline constexpr const char* kSkyBodyDrawShader = R"HLSL(
cbuffer SkyBodyConstants : register(b0) {
    row_major float4x4 gMvp;
    float4 gColor;
};
Texture2D<float4> gTex : register(t0);
SamplerState gSampler : register(s0);

struct VSIn { float3 pos : POSITION; float2 uv : TEXCOORD0; };
struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };

VSOut VSMain(VSIn i) {
    VSOut o;
    o.pos = mul(float4(i.pos, 1.0), gMvp);
    o.uv = i.uv;
    return o;
}

float4 PSMain(VSOut i) : SV_TARGET {
    return gTex.Sample(gSampler, i.uv) * gColor;
}
)HLSL";

} // namespace rs::render

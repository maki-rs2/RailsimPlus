//
// LensFlareShader.h - レンズフレアの要素と白飛びを描くHLSL
//
// 描画先はリニアHDR(R16G16B16A16_FLOAT)。定義の色・画像はsRGBの値なので、リニアへ変換してから
// HDRの倍率(gFlareHdrIntensity・gWhiteoutHdrLevel)を掛ける。
// 光源の可視度(LightVisibility)は、要素の頂点シェーダと白飛びの頂点シェーダが共有する。
// 光源の画面位置の周りの円盤上に SAMPLE_COUNT 点の深度を読み、遮られていない割合を返す。
// レジスタの並び(t0=光源、t1=要素、t2=深度、t3〜=テクスチャ、b0=定数)は LensFlarePassImpl.h の
// ルート署名と、b0 の並びは LensFlarePassImpl.h の FlareConstants と、t0・t1 の並びは
// FlareLightGpu・FlareInstanceGpu と合わせる。
// 太陽(無限遠の光源)の色と強さは、メッシュの直射光・空の太陽と同じ式(SunColorShader.h)で透過率LUT(t11)から引く。
// そのため BuildLensFlareShader は大気の定数と透過率のUV式を本体の前に連結する。
//
#pragma once

#include <string>

#include "../pipeline/ShaderColor.h"
#include "../sky/AtmosphereShader.h"
#include "../sky/SunColorShader.h"

namespace rs::render {

inline constexpr const char* kLensFlareBodyShader = R"HLSL(
#define SAMPLE_COUNT 16
// 太陽(無限遠)の遮蔽の判定。空は深度を書かず遠面(0)のままなので、これより大きければ何かが描かれている。
#define FAR_DEPTH_EPS 1e-6
#define GOLDEN_ANGLE 2.39996323
// 六角形(頂点が+x軸上、外接半径1)の内接半径。
#define HEX_APOTHEM 0.8660254
// 要素の縁の窓(EdgeWindow)。ガウス型の減衰の強さと、縁の手前でsmoothstepが0へ向かい始める位置(縁=1)。見た目で決めた値。
#define GAUSS_K 2.5
#define EDGE_START 0.8

cbuffer FlareConstants : register(b0) {
    row_major float4x4 gViewProj;
    float3 gCameraRight;
    float gSampleRadiusPx;
    float3 gCameraUp;
    float gDepthToleranceRel;
    float3 gCameraPos;
    float gPad0;
    float2 gScreen;
    uint gWhiteoutLight;
    uint gPad1;
    float4 gWhiteoutColor;
    float gFlareHdrIntensity;
    float gWhiteoutHdrLevel;
    uint gSunAtmosphere;  // 0でない: 太陽の光源に透過率LUTの色と強さを掛ける
    float gPad2;
};

struct Light { float3 position; uint atInfinity; };
struct Instance {
    float3 position; float radius;
    float4 innerColor;
    float4 outerColor;
    uint kind; uint textureSlot; uint lightIndex; float brightness;
};

StructuredBuffer<Light> gLights : register(t0);
StructuredBuffer<Instance> gInstances : register(t1);
Texture2D<float> gDepth : register(t2);
Texture2D<float4> gTex0 : register(t3);
Texture2D<float4> gTex1 : register(t4);
Texture2D<float4> gTex2 : register(t5);
Texture2D<float4> gTex3 : register(t6);
Texture2D<float4> gTex4 : register(t7);
Texture2D<float4> gTex5 : register(t8);
Texture2D<float4> gTex6 : register(t9);
Texture2D<float4> gTex7 : register(t10);
Texture2D<float4> gTransmittance : register(t11);
SamplerState gSampler : register(s0);

// 光源の可視度(0〜1)。光源が画面の外(クリップの外・カメラの後ろ)なら1。画面の外のサンプル点は「見える」とする。
// 無限遠(太陽)は、シーンの深度が遠面でない点を遮蔽とみなす。そうでない光源(灯具)は、光源より手前(Reversed-Zで
// 深度が大きい)にシーンがあれば遮蔽とみなす。光源自身のメッシュには遮られないよう、光源をカメラ側へ
// 距離の gDepthToleranceRel 倍だけ寄せた点の深度と比べる。
float LightVisibility(Light light) {
    float4 clip = mul(float4(light.position, 1.0), gViewProj);
    if (clip.w <= 0.0) return 1.0;
    float2 ndc = clip.xy / clip.w;
    if (abs(ndc.x) > 1.0 || abs(ndc.y) > 1.0) return 1.0;
    float2 center = float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5) * gScreen;
    bool atInfinity = light.atInfinity != 0;
    float lightDepth = 0.0;
    if (!atInfinity) {
        float3 nearer = light.position + (gCameraPos - light.position) * gDepthToleranceRel;
        float4 nearerClip = mul(float4(nearer, 1.0), gViewProj);
        lightDepth = nearerClip.z / nearerClip.w;
    }
    float visible = 0.0;
    [unroll]
    for (uint i = 0; i < SAMPLE_COUNT; ++i) {
        float angle = (float)i * GOLDEN_ANGLE;
        float radius = sqrt(((float)i + 0.5) / (float)SAMPLE_COUNT) * gSampleRadiusPx;
        float2 p = center + radius * float2(cos(angle), sin(angle));
        if (p.x < 0.0 || p.y < 0.0 || p.x >= gScreen.x || p.y >= gScreen.y) {
            visible += 1.0;
            continue;
        }
        float sceneDepth = gDepth.Load(int3((int2)p, 0));
        bool occluded = atInfinity ? (sceneDepth > FAR_DEPTH_EPS) : (sceneDepth > lightDepth);
        visible += occluded ? 0.0 : 1.0;
    }
    return visible / (float)SAMPLE_COUNT;
}

struct ElementVSOut {
    float4 pos : SV_POSITION;
    float2 local : TEXCOORD0;
    nointerpolation uint instance : TEXCOORD1;
    nointerpolation float visibility : TEXCOORD2;
};

ElementVSOut VSElement(uint vid : SV_VertexID, uint iid : SV_InstanceID) {
    // 2つの三角形で作る板。角は (-1,-1)〜(1,1)。
    static const float2 kCorners[6] = {
        float2(-1.0, -1.0), float2(1.0, -1.0), float2(-1.0, 1.0),
        float2(-1.0, 1.0), float2(1.0, -1.0), float2(1.0, 1.0)};
    Instance inst = gInstances[iid];
    float2 corner = kCorners[vid];
    float3 world = inst.position + (gCameraRight * corner.x + gCameraUp * corner.y) * inst.radius;
    ElementVSOut o;
    o.pos = mul(float4(world, 1.0), gViewProj);
    o.local = corner;
    o.instance = iid;
    o.visibility = LightVisibility(gLights[inst.lightIndex]);
    return o;
}

// 頂点が+x軸上の六角形のノルム。辺の上で1になる。
float HexNorm(float2 p) {
    float a = abs(p.x * HEX_APOTHEM + p.y * 0.5);
    float b = abs(p.y);
    float c = abs(-p.x * HEX_APOTHEM + p.y * 0.5);
    return max(a, max(b, c)) / HEX_APOTHEM;
}

// 円・六角形の縁(t=1)へ向かって落ちる窓。ガウス型の減衰に、縁の手前から0へ向かうsmoothstepを掛け、
// 縁がくっきり出ないようにする。t は中心0・縁1。
float EdgeWindow(float t) {
    return exp(-GAUSS_K * t * t) * (1.0 - smoothstep(EDGE_START, 1.0, t));
}

float4 SampleSlot(uint slot, float2 uv) {
    if (slot == 0) return gTex0.SampleLevel(gSampler, uv, 0);
    if (slot == 1) return gTex1.SampleLevel(gSampler, uv, 0);
    if (slot == 2) return gTex2.SampleLevel(gSampler, uv, 0);
    if (slot == 3) return gTex3.SampleLevel(gSampler, uv, 0);
    if (slot == 4) return gTex4.SampleLevel(gSampler, uv, 0);
    if (slot == 5) return gTex5.SampleLevel(gSampler, uv, 0);
    if (slot == 6) return gTex6.SampleLevel(gSampler, uv, 0);
    return gTex7.SampleLevel(gSampler, uv, 0);
}

// 太陽に掛ける色(rgb)と強さ(a)。大気を通った太陽の透過率の最大成分を強さ、それで割った色を色合いにする
// (強さを色にも掛けると、白飛びが画面を暗くする向きに働くため)。スイッチが切れているか、太陽でない光源は(1,1,1,1)。
float4 SunAtmosphereTint(Light light) {
    if (gSunAtmosphere == 0 || light.atInfinity == 0) return float4(1.0, 1.0, 1.0, 1.0);
    float3 toSun = normalize(light.position - gCameraPos);
    float3 sunColor = SunColorFromTransmittance(gTransmittance, gSampler, toSun);
    float strength = max(sunColor.r, max(sunColor.g, sunColor.b));
    if (strength <= 1e-4) return float4(1.0, 1.0, 1.0, 0.0);
    return float4(sunColor / strength, strength);
}

float4 PSElement(ElementVSOut i) : SV_TARGET {
    Instance inst = gInstances[i.instance];
    float4 color;
    if (inst.kind == 2) {
        // 画像は窓を掛けず、画像のアルファのまま。
        float2 uv = float2(i.local.x * 0.5 + 0.5, 0.5 - i.local.y * 0.5);
        color = SampleSlot(inst.textureSlot, uv) * inst.innerColor;
    } else {
        float t = (inst.kind == 1) ? HexNorm(i.local) : length(i.local);
        if (t > 1.0) discard;
        color = lerp(inst.innerColor, inst.outerColor, t);
        color.a *= EdgeWindow(t);
    }
    // 明るさの係数は色のアルファだけに掛ける(RGBは変えない)。
    color.a *= saturate(inst.brightness) * i.visibility;
    float4 sun = SunAtmosphereTint(gLights[inst.lightIndex]);
    color.a *= sun.a;
    color.rgb = LinearFromSrgb(saturate(color.rgb)) * gFlareHdrIntensity * sun.rgb;
    return color;
}

struct WhiteoutVSOut {
    float4 pos : SV_POSITION;
    nointerpolation float visibility : TEXCOORD0;
};

WhiteoutVSOut VSWhiteout(uint vid : SV_VertexID) {
    float2 p = float2((vid << 1) & 2, vid & 2); // (0,0),(2,0),(0,2)
    WhiteoutVSOut o;
    o.pos = float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
    o.visibility = LightVisibility(gLights[gWhiteoutLight]);
    return o;
}

float4 PSWhiteout(WhiteoutVSOut i) : SV_TARGET {
    float4 sun = SunAtmosphereTint(gLights[gWhiteoutLight]);
    return float4(LinearFromSrgb(saturate(gWhiteoutColor.rgb)) * gWhiteoutHdrLevel * sun.rgb, gWhiteoutColor.a * i.visibility * sun.a);
}
)HLSL";

inline std::string BuildLensFlareShader() {
    return BuildAtmosphereConstantsShader() + kTransmittanceUvShader + kSunColorShader + kSrgbTransferShader +
           kLensFlareBodyShader;
}

} // namespace rs::render

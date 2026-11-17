#pragma once

#include <string>

#include "../resource/RenderTuning.h"
#include "../sky/AtmosphereShader.h"
#include "../sky/ExposureShader.h"
#include "../sky/SunColorShader.h"
#include "ParticlePass.h"

namespace rs::render {
namespace particle_detail {

inline constexpr std::uint32_t kRootInstances = 0;
inline constexpr std::uint32_t kRootTextures = 1;
inline constexpr std::uint32_t kRootConstants = 2;
inline constexpr std::uint32_t kRootBindless = 3;

struct Constants {
    float viewProj[16];
    float cameraRight[3];
    float padding0;
    float cameraUp[3];
    float padding1;
    float depthParams[4];
    float lightDir[4];
    float pluginDirectional[4];
    float pluginAmbient[4];
    float moonLight[4];
    std::uint32_t indices[4];
    std::uint32_t adaptation[4];
};
inline constexpr std::uint32_t kConstantDwords = sizeof(Constants) / sizeof(std::uint32_t);
// ルートSRVの2 DWORDとテーブル2つの各1 DWORDを含め、ルート署名の上限に収める。
static_assert(kConstantDwords + 4 <= 64);

} // namespace particle_detail

inline constexpr const char* kParticleConstantsShader = R"HLSL(
cbuffer ParticleConstants : register(b0) {
    row_major float4x4 gViewProj;
    float3 gCameraRight; float gPad0;
    float3 gCameraUp; float gPad1;
    float4 gDepthParams; // xy=Reversed-Zの射影係数 z=拡散IBL倍率
    float4 gLightDir;
    float4 gPluginDirectional;
    float4 gPluginAmbient;
    float4 gMoonLight;
    uint4 gIndices; // x=拡散キューブ y=透過率LUT z=IBL有効 w=シーン深度
    uint4 gAdaptation;
};
SamplerState gSampler : register(s0);
)HLSL";

inline constexpr const char* kParticleBodyShader = R"HLSL(
struct Instance {
    float3 position; float radius;
    float4 color;
    float angleRadians; uint textureSlot; uint blendMode; float padding;
    float3 velocity; float depth;
    float3 stretchDirection; float length;
};
StructuredBuffer<Instance> gInstances : register(t0);
Texture2D<float4> gTex[MAX_PARTICLE_TEXTURES] : register(t1);

float3 ParticleSunColor(float3 toLight) {
    Texture2D<float4> lut = gBindlessTex2D[gIndices.y];
    return SunColorFromTransmittance(lut, gSampler, toLight);
}

// 太陽が照らす直射光(太陽色×強さの調整値。式は SunColorShader.h)。月の光には ParticleSunColor を使う。
float3 ParticleSunLight(float3 toSun) {
    Texture2D<float4> lut = gBindlessTex2D[gIndices.y];
    return SunLightFromTransmittance(lut, gSampler, toSun);
}

float3 ParticleLighting(float3 toCamera) {
    bool iblOn = gIndices.z != 0u;
    bool pluginSky = iblOn && gPluginDirectional.w > 0.5;
    bool standardSky = iblOn && !pluginSky;
    float dayAlpha = saturate(gLightDir.w);
    float3 toSun = normalize(-gLightDir.xyz);
    float adaptationGain = AdaptationGainFromTexture();
    float3 sunColor = float3(1.0, 1.0, 1.0);
    if (iblOn) sunColor = pluginSky ? gPluginDirectional.rgb : ParticleSunLight(toSun) * adaptationGain;
    // 標準の空は透過率と地球の影で日没を扱うため、昼夜係数を重ねて掛けない。
    float directFade = standardSky ? 1.0 : dayAlpha;
    float3 direct = sunColor * directFade * saturate(0.5 + 0.5 * dot(toCamera, toSun));
    if (standardSky && gMoonLight.w > 0.0) {
        float3 toMoon = normalize(gMoonLight.xyz);
        direct += ParticleSunColor(toMoon) * (gMoonLight.w * adaptationGain) *
                  saturate(0.5 + 0.5 * dot(toCamera, toMoon));
    }
    float3 ambient;
    if (pluginSky) {
        // プラグインの環境光は空側で昼夜の色を持つので、生成IBLの倍率を掛けない。
        ambient = gPluginAmbient.rgb;
    } else {
        float3 nightFloor = KNIGHT_AMBIENT_INTENSITY * float3(0.05, 0.06, 0.09) * (1.0 - dayAlpha);
        if (gAdaptation.y != 0u) nightFloor = float3(0.0, 0.0, 0.0);
        ambient = KAMBIENT_INTENSITY * dayAlpha;
        if (iblOn) {
            TextureCube<float4> cube = gBindlessTexCube[gIndices.x];
            // 位置も基底もワールド空間なので、キューブへ渡す方向にビュー回転を掛けない。
            ambient = 0.5 * (cube.SampleLevel(gSampler, float3(0.0, 1.0, 0.0), 0).rgb +
                             cube.SampleLevel(gSampler, toCamera, 0).rgb) * gDepthParams.z;
            if (gAdaptation.y != 0u) ambient = max(ambient, KNIGHT_AMBIENT_FLOOR * float3(0.8, 1.0, 1.4));
        }
        ambient += nightFloor;
        if (standardSky) ambient *= IblExposureCorrection();
    }
    return ambient * KPARTICLE_AMBIENT_SCALE + direct * KPARTICLE_SUN_SCALE;
}

struct VSOut {
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    nointerpolation uint instance : TEXCOORD1;
};

VSOut VSMain(uint vid : SV_VertexID, uint iid : SV_InstanceID) {
    // 三角形ストリップの4隅。UVは回転前の板に固定する。
    static const float2 kCorners[4] = {
        float2(-1.0, -1.0), float2(-1.0, 1.0), float2(1.0, -1.0), float2(1.0, 1.0)};
    Instance inst = gInstances[iid];
    float2 corner = kCorners[vid];
    float sine, cosine;
    sincos(inst.angleRadians, sine, cosine);
    float2 rotated = float2(cosine * corner.x - sine * corner.y, sine * corner.x + cosine * corner.y);
    float2 offset = inst.radius * rotated;
    if (inst.length > 2.0 * inst.radius) {
        float2 direction = float2(dot(inst.stretchDirection, gCameraRight), dot(inst.stretchDirection, gCameraUp));
        float2 perpendicular = float2(-direction.y, direction.x);
        // 長辺を速度方向へ向けるので、乱数の回転角は使わない。
        offset = direction * (corner.y * inst.length * 0.5) + perpendicular * (corner.x * inst.radius);
    }
    float3 relative = inst.position + gCameraRight * offset.x + gCameraUp * offset.y;
    VSOut o;
    o.position = mul(float4(relative, 1.0), gViewProj);
    o.uv = float2(corner.x * 0.5 + 0.5, 0.5 - corner.y * 0.5);
    o.instance = iid;
    return o;
}

float4 PSMain(VSOut i) : SV_TARGET {
    Instance inst = gInstances[i.instance];
    // sRGBのSRVが画像のRGBだけをリニアに変換する。粒のRGBは既にリニア。
    float4 color = inst.color * gTex[NonUniformResourceIndex(inst.textureSlot)].Sample(gSampler, i.uv);
    Texture2D<float> sceneDepth = gBindlessTex2DR[gIndices.w];
    float depth = sceneDepth.Load(int3(int2(i.position.xy), 0));
    // depth = A + B / viewDepth。クリア値0も遠クリップの距離へ戻る。
    float sceneDepthMeters = gDepthParams.y / (depth - gDepthParams.x);
    float alpha = color.a * saturate((sceneDepthMeters - inst.depth) / KPARTICLE_SOFT_DEPTH_METERS);
    if (inst.blendMode != PARTICLE_BLEND_ADD) color.rgb *= ParticleLighting(normalize(-inst.position));
    return float4(color.rgb * alpha, inst.blendMode == PARTICLE_BLEND_ADD ? 0.0 : alpha);
}
)HLSL";

inline std::string BuildParticleShader() {
    const ParticleRenderTuning& tuning = ParticleRenderTuning::Embedded();
    const RenderTuning& renderTuning = RenderTuning::Embedded();
    return "#define MAX_PARTICLE_TEXTURES " + std::to_string(kMaxParticleTextures) +
           "\n#define PARTICLE_BLEND_ADD " +
           std::to_string(static_cast<std::uint32_t>(rs::legacy_pi::ParticleBlendMode::Add)) +
           "\n#define KPARTICLE_SOFT_DEPTH_METERS " + std::to_string(tuning.softDepthMeters) +
           "\n#define KPARTICLE_AMBIENT_SCALE " + std::to_string(tuning.ambientScale) +
           "\n#define KPARTICLE_SUN_SCALE " + std::to_string(tuning.sunScale) +
           "\n#define KAMBIENT_INTENSITY " + std::to_string(renderTuning.ambientIntensity) +
           "\n#define KNIGHT_AMBIENT_INTENSITY " + std::to_string(renderTuning.nightAmbientIntensity) +
           "\n#define KNIGHT_AMBIENT_FLOOR " + std::to_string(renderTuning.nightAmbientFloor) +
           "\n" + kParticleConstantsShader + BuildAtmosphereConstantsShader() + kTransmittanceUvShader +
           kSunColorShader + kExposureShader + kParticleBodyShader;
}

} // namespace rs::render

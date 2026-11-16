//
// LensFlareGpuTypes.h - レンズフレアのパスがGPUへ送る型と、アップロードバッファの配置
//
// LensFlareShader.h の同名の型と並びを合わせる。DX12の型は持たない。
//
#pragma once

#include <cstdint>

#include "LensFlarePass.h"

namespace rs::render {

// ルート署名の並び。
constexpr std::uint32_t kRootLights = 0;        // t0 ルートSRV(StructuredBuffer<Light>)
constexpr std::uint32_t kRootInstances = 1;     // t1 ルートSRV(StructuredBuffer<Instance>)
constexpr std::uint32_t kRootDepthTable = 2;    // t2 深度(1枠)
constexpr std::uint32_t kRootTextureTable = 3;  // t3〜 テクスチャ(kMaxFlareTextures 枠)
constexpr std::uint32_t kRootConstants = 4;     // b0
constexpr std::uint32_t kRootTransmittanceTable = 5;  // 大気の透過率LUT(1枠)
// 透過率LUTのレジスタ番号(t11。深度t2、テクスチャt3〜t10の次)。LensFlareShader.h の gTransmittance と合わせる。
constexpr std::uint32_t kTransmittanceRegister = 3 + kMaxFlareTextures;

struct FlareLightGpu {
    float position[3];
    std::uint32_t atInfinity;
};
static_assert(sizeof(FlareLightGpu) == 16);

struct FlareInstanceGpu {
    float position[3];
    float radius;
    float innerColor[4];
    float outerColor[4];
    std::uint32_t kind;
    std::uint32_t textureSlot;
    std::uint32_t lightIndex;
    float brightness;
};
static_assert(sizeof(FlareInstanceGpu) == 64);

// HLSL の cbuffer FlareConstants と同じ並び。
struct FlareConstants {
    float viewProj[16];
    float cameraRight[3];
    float sampleRadiusPx;
    float cameraUp[3];
    float depthToleranceRel;
    float cameraPos[3];
    float pad0;
    float screen[2];
    std::uint32_t whiteoutLight;
    std::uint32_t pad1;
    float whiteoutColor[4];
    float flareHdrIntensity;
    float whiteoutHdrLevel;
    std::uint32_t sunAtmosphere;
    float pad2;
};
static_assert(sizeof(FlareConstants) == 160);
constexpr std::uint32_t kFlareConstantDwords = sizeof(FlareConstants) / sizeof(std::uint32_t);
static_assert(sizeof(FlareConstants) % sizeof(std::uint32_t) == 0);

// 1フレーム分の書き込み先(UPLOADバッファ。先頭に光源、kFlareInstanceOffset から要素)。
constexpr std::uint64_t kFlareLightBytes = sizeof(FlareLightGpu) * kMaxFlareLights;
constexpr std::uint64_t kFlareInstanceOffset = 256;  // 光源の末尾以降で、256バイトに揃えた位置
constexpr std::uint64_t kFlareUploadBytes = kFlareInstanceOffset + sizeof(FlareInstanceGpu) * kMaxFlareInstances;
static_assert(kFlareLightBytes <= kFlareInstanceOffset);

} // namespace rs::render

#pragma once

#include <cstdint>
#include <memory>
#include <span>

#include "../resource/TextureLoader.h"
#include "../rhi/CommandList.h"
#include "../rhi/CommandQueue.h"
#include "../rhi/CpuDescriptor.h"
#include "../rhi/Device.h"
#include "../rhi/SrvHeap.h"
#include "ParticleBatchBuilder.h"

namespace rs::render {

// シェーダのテクスチャ配列と、共有SRVヒープに予約する画像の数。
inline constexpr std::uint32_t kMaxParticleTextures = 16;

struct ParticleFrame {
    rs::core::Mat4 view = rs::core::Mat4::Identity();
    rs::core::Mat4 viewProj = rs::core::Mat4::Identity();
    // Reversed-Zの射影行列のm[10]とm[14]。深度値から視線方向のmへ戻す。
    float depthProjection[2] = {};
    float iblDiffuseIntensity = 0.0f;
    float lightDir[4] = {};
    float pluginDirectional[4] = {};
    float pluginAmbient[4] = {};
    float moonLight[4] = {};
    std::uint32_t diffuseCubeSrvIndex = 0;
    std::uint32_t transmittanceSrvIndex = 0;
    bool iblEnabled = false;
    std::uint32_t adaptation[2] = {};
    std::span<const ParticleGpuInstance> instances;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t frameIndex = 0;
};

class ParticlePass {
public:
    // 先頭の1枠は深度用に予約し、このパスでは書き込まない。
    static constexpr std::uint32_t kSrvCount = 1 + kMaxParticleTextures;
    static constexpr std::uint32_t kRingSize = 2;

    ParticlePass();
    ~ParticlePass();
    ParticlePass(const ParticlePass&) = delete;
    ParticlePass& operator=(const ParticlePass&) = delete;

    bool Init(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap,
              const rs::rhi::CommandQueue& queue, std::uint32_t firstSrvIndex, std::size_t maxParticles);
    bool Ready() const;
    std::uint32_t DepthSrvIndex() const;
    bool UploadTexture(std::uint32_t slot, const ImageData& image);
    bool HasTexture(std::uint32_t slot) const;
    // depthDsv は読み取り専用DSV。呼び出し側が深度を読み取り状態へ遷移させる。
    void Record(const rs::rhi::CommandList& commandList, rs::rhi::CpuDescriptor rtv,
                rs::rhi::CpuDescriptor depthDsv, const ParticleFrame& frame) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rs::render

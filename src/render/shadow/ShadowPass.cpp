#include "ShadowPass.h"

#include <d3d12.h>
#include <wrl/client.h>

#include <cstring>

#include <spdlog/spdlog.h>

#include "../pipeline/MeshPipeline.h"

using Microsoft::WRL::ComPtr;

namespace rs::render {

namespace {
// 影定数バッファを持つフレーム数(GPUが読んでいる最中のフレームを上書きしないため、MeshPipeline と同じ2)。
constexpr std::uint32_t kShadowFrames = 2;
} // namespace

struct ShadowPass::Impl {
    std::array<ComPtr<ID3D12Resource>, kShadowFrames> constantBuffers;
    std::array<std::uint8_t*, kShadowFrames> mapped = {};
};

ShadowPass::ShadowPass() = default;
ShadowPass::~ShadowPass() = default;

bool ShadowPass::Init(const rs::rhi::Device& device) {
    impl_.reset();
    if (!device.Valid()) return false;
    std::unique_ptr<Impl> impl = std::make_unique<Impl>();
    for (std::uint32_t frame = 0; frame < kShadowFrames; ++frame) {
        if (!CreatePooledUploadBuffer(device.Native(), sizeof(ShadowConstants),
                                      impl->constantBuffers[frame])) {
            spdlog::error("[shadow_pass] 影定数バッファの確保に失敗しました");
            return false;
        }
        const D3D12_RANGE noRead = {0, 0};
        if (FAILED(impl->constantBuffers[frame]->Map(0, &noRead, reinterpret_cast<void**>(&impl->mapped[frame])))) {
            spdlog::error("[shadow_pass] 影定数バッファのMapに失敗しました frame={}", frame);
            return false;
        }
    }
    impl_ = std::move(impl);
    return true;
}

bool ShadowPass::Ready() const { return impl_ != nullptr; }

std::uint64_t ShadowPass::WriteConstants(std::uint32_t frameIndex, const ShadowConstants& constants) {
    if (!Ready()) return 0;
    const std::uint32_t frame = frameIndex % kShadowFrames;
    std::memcpy(impl_->mapped[frame], &constants, sizeof(ShadowConstants));
    return impl_->constantBuffers[frame]->GetGPUVirtualAddress();
}

void ShadowPass::Record(const rs::rhi::CommandList& commandList, ShadowMap& shadowMap,
                        std::uint32_t slice, const MeshConstants& lightBase,
                        const std::vector<ShadowCaster>& casters, const ShadowExtraRecord& extraRecord,
                        std::uint32_t frameIndex) const {
    if (!Ready() || !shadowMap.Ready() || !commandList.Valid()) return;
    shadowMap.Transition(commandList, ShadowMapState::DepthWrite);
    shadowMap.BeginSlice(commandList, slice);

    const MeshConstants& lightConstants = lightBase;
    for (const ShadowCaster& caster : casters) {
        if (caster.pipeline == nullptr) continue;
        caster.pipeline->RecordShadowInstancedMultiPart(commandList, lightConstants,
                                                        frameIndex, slice, 1, caster.partCount, caster.includeThin);
    }
    if (extraRecord) extraRecord(lightConstants);
    shadowMap.Transition(commandList, ShadowMapState::ShaderResource);
}

} // namespace rs::render

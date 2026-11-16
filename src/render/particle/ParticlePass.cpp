#include "ParticlePass.h"

#include <d3d12.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <exception>
#include <future>

#include <ResourceUploadBatch.h>
#include <spdlog/spdlog.h>

#include "../pipeline/MeshPipelineTypes.h"
#include "../resource/TextureUpload.h"
#include "ParticleShader.h"

using Microsoft::WRL::ComPtr;

namespace rs::render {

bool CreateParticlePipeline(ID3D12Device* device, ComPtr<ID3D12RootSignature>& rootSig,
                              ComPtr<ID3D12PipelineState>& pso);

struct ParticlePass::Impl {
    ID3D12Device* device = nullptr;
    ID3D12CommandQueue* queue = nullptr;
    ID3D12DescriptorHeap* srvHeap = nullptr;
    UINT srvStride = 0;
    std::uint32_t firstTextureSrvIndex = 0;
    std::size_t capacity = 0;
    ComPtr<ID3D12RootSignature> rootSig;
    ComPtr<ID3D12PipelineState> pso;
    std::array<ComPtr<ID3D12Resource>, kRingSize> buffers;
    std::array<void*, kRingSize> mapped = {};
    std::array<ComPtr<ID3D12Resource>, kMaxParticleTextures> textures;
};

ParticlePass::ParticlePass() = default;
ParticlePass::~ParticlePass() = default;

bool ParticlePass::Init(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap,
                         const rs::rhi::CommandQueue& queue, std::uint32_t firstSrvIndex, std::size_t maxParticles) {
    impl_.reset();
    if (!device.Valid() || !srvHeap.Valid() || !queue.Valid()) return false;
    std::unique_ptr<Impl> impl = std::make_unique<Impl>();
    impl->device = device.Native();
    impl->queue = queue.Native();
    impl->srvHeap = srvHeap.Native();
    impl->srvStride = srvHeap.Stride();
    // 先頭は深度用の予約枠で、画像のテーブルには含めない。
    impl->firstTextureSrvIndex = firstSrvIndex + 1;
    impl->capacity = maxParticles;
    if (!CreateParticlePipeline(impl->device, impl->rootSig, impl->pso)) return false;
    const std::size_t bytes = std::max<std::size_t>(1, impl->capacity) * sizeof(ParticleGpuInstance);
    for (std::uint32_t ring = 0; ring < kRingSize; ++ring) {
        if (!CreatePooledUploadBuffer(impl->device, bytes, impl->buffers[ring]) ||
            FAILED(impl->buffers[ring]->Map(0, nullptr, &impl->mapped[ring]))) {
            spdlog::error("[particle] instance upload buffer creation or mapping failed");
            return false;
        }
        impl->buffers[ring]->SetName(L"ParticleInstances");
    }
    for (std::uint32_t slot = 0; slot < kMaxParticleTextures; ++slot) {
        WriteTextureSrv(impl->device, impl->srvHeap, impl->srvStride, nullptr,
                         impl->firstTextureSrvIndex + slot, true);
    }
    impl_ = std::move(impl);
    return true;
}

bool ParticlePass::Ready() const { return impl_ != nullptr; }

std::uint32_t ParticlePass::DepthSrvIndex() const {
    return impl_ ? impl_->firstTextureSrvIndex - 1 : 0;
}

bool ParticlePass::HasTexture(std::uint32_t slot) const {
    return impl_ && slot < kMaxParticleTextures && impl_->textures[slot] != nullptr;
}

bool ParticlePass::UploadTexture(std::uint32_t slot, const ImageData& image) {
    if (!impl_ || slot >= kMaxParticleTextures || !image.ok || impl_->textures[slot]) return false;
    try {
        DirectX::ResourceUploadBatch upload(impl_->device);
        upload.Begin();
        ComPtr<ID3D12Resource> texture = CreateGpuTexture(impl_->device, upload, image, 0, true);
        std::future<void> finished = upload.End(impl_->queue);
        finished.get();
        if (!texture) return false;
        impl_->textures[slot] = std::move(texture);
        WriteTextureSrv(impl_->device, impl_->srvHeap, impl_->srvStride, impl_->textures[slot].Get(),
                         impl_->firstTextureSrvIndex + slot, true);
        return true;
    } catch (const std::exception& error) {
        spdlog::warn("[particle] texture upload failed: {}", error.what());
        return false;
    }
}

void ParticlePass::Record(const rs::rhi::CommandList& commandList, rs::rhi::CpuDescriptor rtv,
                           rs::rhi::CpuDescriptor depthDsv, const ParticleFrame& frame) const {
    if (!impl_ || !commandList.Valid() || !rtv.Valid() || !depthDsv.Valid() || frame.instances.empty() ||
        frame.width == 0 || frame.height == 0) return;
    const std::size_t count = std::min(frame.instances.size(), impl_->capacity);
    if (count == 0) return;
    const std::uint32_t ring = frame.frameIndex % kRingSize;
    std::memcpy(impl_->mapped[ring], frame.instances.data(), count * sizeof(ParticleGpuInstance));

    using namespace particle_detail;
    Constants constants = {};
    std::memcpy(constants.viewProj, frame.viewProj.m, sizeof(constants.viewProj));
    // 行ベクトルのビュー行列の列から、ワールドでのカメラの右と上を取る。
    for (std::size_t axis = 0; axis < 3; ++axis) {
        constants.cameraRight[axis] = frame.view.m[axis * 4];
        constants.cameraUp[axis] = frame.view.m[axis * 4 + 1];
    }
    constants.depthParams[0] = frame.depthProjection[0];
    constants.depthParams[1] = frame.depthProjection[1];
    constants.depthParams[2] = frame.iblDiffuseIntensity;
    std::memcpy(constants.lightDir, frame.lightDir, sizeof(constants.lightDir));
    std::memcpy(constants.pluginDirectional, frame.pluginDirectional, sizeof(constants.pluginDirectional));
    std::memcpy(constants.pluginAmbient, frame.pluginAmbient, sizeof(constants.pluginAmbient));
    std::memcpy(constants.moonLight, frame.moonLight, sizeof(constants.moonLight));
    constants.indices[0] = frame.diffuseCubeSrvIndex;
    constants.indices[1] = frame.transmittanceSrvIndex;
    constants.indices[2] = frame.iblEnabled ? 1u : 0u;
    constants.indices[3] = DepthSrvIndex();
    std::memcpy(constants.adaptation, frame.adaptation, sizeof(frame.adaptation));
    ID3D12GraphicsCommandList* list = commandList.Native();
    const D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = {rtv.ptr};
    const D3D12_CPU_DESCRIPTOR_HANDLE depthHandle = {depthDsv.ptr};
    list->OMSetRenderTargets(1, &rtvHandle, FALSE, &depthHandle);
    const D3D12_VIEWPORT viewport = {0.0f, 0.0f, static_cast<float>(frame.width), static_cast<float>(frame.height),
                                     0.0f, 1.0f};
    const D3D12_RECT scissor = {0, 0, static_cast<LONG>(frame.width), static_cast<LONG>(frame.height)};
    list->RSSetViewports(1, &viewport);
    list->RSSetScissorRects(1, &scissor);
    ID3D12DescriptorHeap* heaps[] = {impl_->srvHeap};
    list->SetDescriptorHeaps(1, heaps);
    list->SetGraphicsRootSignature(impl_->rootSig.Get());
    list->SetPipelineState(impl_->pso.Get());
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    list->SetGraphicsRootShaderResourceView(kRootInstances, impl_->buffers[ring]->GetGPUVirtualAddress());
    D3D12_GPU_DESCRIPTOR_HANDLE textures = impl_->srvHeap->GetGPUDescriptorHandleForHeapStart();
    textures.ptr += static_cast<UINT64>(impl_->firstTextureSrvIndex) * impl_->srvStride;
    list->SetGraphicsRootDescriptorTable(kRootTextures, textures);
    list->SetGraphicsRoot32BitConstants(kRootConstants, kConstantDwords, &constants, 0);
    constexpr UINT kQuadVertexCount = 4; // 三角形ストリップで作る板。
    list->DrawInstanced(kQuadVertexCount, static_cast<UINT>(count), 0, 0);
}

} // namespace rs::render

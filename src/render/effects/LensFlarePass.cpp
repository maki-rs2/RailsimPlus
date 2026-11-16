#include "LensFlarePass.h"

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
#include "LensFlareGpuTypes.h"

using Microsoft::WRL::ComPtr;

namespace rs::render {

// LensFlarePassPipeline.cpp にある定義(同じ宣言をそちらも持つ。DX12型を出さないためヘッダは置かない)。
bool CreateLensFlarePipelines(ID3D12Device* device, ComPtr<ID3D12RootSignature>& rootSig,
                              ComPtr<ID3D12PipelineState>& elementPso, ComPtr<ID3D12PipelineState>& whiteoutPso);

namespace {

// テクスチャはsRGBの値のままシェーダで引き、シェーダ内でリニアへ変換するので、sRGB形式ではなくUNORMで引く。
constexpr DXGI_FORMAT kTextureFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr std::uint32_t kDepthSrvOffset = 0;
constexpr std::uint32_t kFirstTextureSrvOffset = 1;

void CopyColor(float out[4], const FlareColor& color) {
    out[0] = color.r;
    out[1] = color.g;
    out[2] = color.b;
    out[3] = color.a;
}

// 要素1件をGPU用に詰める。
FlareInstanceGpu PackInstance(const FlareDraw& draw) {
    const FlareElementSpec& element = *draw.instance.element;
    FlareInstanceGpu gpu = {};
    gpu.position[0] = draw.instance.position.x;
    gpu.position[1] = draw.instance.position.y;
    gpu.position[2] = draw.instance.position.z;
    gpu.radius = draw.instance.radius;
    CopyColor(gpu.innerColor, element.innerColor);
    CopyColor(gpu.outerColor, element.outerColor);
    gpu.kind = static_cast<std::uint32_t>(element.kind);
    gpu.textureSlot = element.textureIndex;
    gpu.lightIndex = draw.lightIndex;
    gpu.brightness = draw.instance.brightness;
    return gpu;
}

} // namespace

struct LensFlarePass::Impl {
    ID3D12Device* device = nullptr;
    ID3D12CommandQueue* queue = nullptr;
    ID3D12DescriptorHeap* srvHeap = nullptr;
    UINT srvStride = 0;
    std::uint32_t firstSrvIndex = 0;
    ComPtr<ID3D12RootSignature> rootSig;
    ComPtr<ID3D12PipelineState> elementPso;
    ComPtr<ID3D12PipelineState> whiteoutPso;
    std::array<ComPtr<ID3D12Resource>, kRingSize> uploadBuffers;
    std::array<std::uint8_t*, kRingSize> uploadMapped = {};
    std::array<ComPtr<ID3D12Resource>, kMaxFlareTextures> textures;
    bool warnedOverflow = false;

    bool CreateUploadBuffers();
    void WriteTextureSrv(std::uint32_t slot, ID3D12Resource* texture);
    bool UploadTexture(const ImageData& image, ComPtr<ID3D12Resource>& out);
    D3D12_GPU_DESCRIPTOR_HANDLE GpuHandle(std::uint32_t srvIndex) const;
    // frame を uploadMapped の 1 本へ詰める。描く要素の数を返す(上限と、描けない要素は除く)。
    std::uint32_t PackFrame(const LensFlareFrame& frame, std::uint8_t* base, std::uint32_t lightCount);
};

bool LensFlarePass::Impl::CreateUploadBuffers() {
    for (std::uint32_t i = 0; i < kRingSize; ++i) {
        if (!CreatePooledUploadBuffer(device, kFlareUploadBytes, uploadBuffers[i])) return false;
        void* mapped = nullptr;
        if (FAILED(uploadBuffers[i]->Map(0, nullptr, &mapped))) {
            spdlog::error("[lens_flare] 頂点バッファのMapに失敗しました");
            return false;
        }
        uploadMapped[i] = static_cast<std::uint8_t*>(mapped);
    }
    return true;
}

D3D12_GPU_DESCRIPTOR_HANDLE LensFlarePass::Impl::GpuHandle(std::uint32_t srvIndex) const {
    D3D12_GPU_DESCRIPTOR_HANDLE handle = srvHeap->GetGPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<UINT64>(srvIndex) * srvStride;
    return handle;
}

void LensFlarePass::Impl::WriteTextureSrv(std::uint32_t slot, ID3D12Resource* texture) {
    D3D12_SHADER_RESOURCE_VIEW_DESC view = {};
    view.Format = kTextureFormat;
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Texture2D.MipLevels = 1;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = srvHeap->GetCPUDescriptorHandleForHeapStart();
    cpu.ptr += static_cast<SIZE_T>(firstSrvIndex + kFirstTextureSrvOffset + slot) * srvStride;
    // texture が nullptr のときは、何も読まないSRV(未使用の枠をテーブルに含めても妥当にするため)。
    device->CreateShaderResourceView(texture, &view, cpu);
}

bool LensFlarePass::Impl::UploadTexture(const ImageData& image, ComPtr<ID3D12Resource>& out) {
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = image.width;
    desc.Height = image.height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = kTextureFormat;
    desc.SampleDesc.Count = 1;
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
                                               nullptr, IID_PPV_ARGS(&out))))
        return false;
    try {
        DirectX::ResourceUploadBatch upload(device);
        upload.Begin();
        D3D12_SUBRESOURCE_DATA sub = {};
        sub.pData = image.pixels.data();
        sub.RowPitch = static_cast<LONG_PTR>(image.width) * 4;
        sub.SlicePitch = sub.RowPitch * static_cast<LONG_PTR>(image.height);
        upload.Upload(out.Get(), 0, &sub, 1);
        upload.Transition(out.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        std::future<void> finished = upload.End(queue);
        finished.get();
    } catch (const std::exception& e) {
        spdlog::warn("[lens_flare] テクスチャのアップロードに失敗しました: {}", e.what());
        return false;
    }
    return true;
}

std::uint32_t LensFlarePass::Impl::PackFrame(const LensFlareFrame& frame, std::uint8_t* base,
                                             std::uint32_t lightCount) {
    FlareLightGpu* lights = reinterpret_cast<FlareLightGpu*>(base);
    for (std::uint32_t i = 0; i < lightCount; ++i) {
        const FlareLight& light = frame.lights[i];
        lights[i] = {{light.position.x, light.position.y, light.position.z}, light.atInfinity ? 1u : 0u};
    }
    FlareInstanceGpu* instances = reinterpret_cast<FlareInstanceGpu*>(base + kFlareInstanceOffset);
    std::uint32_t count = 0;
    for (const FlareDraw& draw : frame.draws) {
        if (draw.instance.element == nullptr || draw.lightIndex >= lightCount) continue;
        const FlareElementSpec& element = *draw.instance.element;
        const bool textureMissing =
            element.kind == FlareElementKind::Texture &&
            (element.textureIndex >= kMaxFlareTextures || !textures[element.textureIndex]);
        if (textureMissing) continue;  // 画像の無いテクスチャ要素は描かない
        if (count >= kMaxFlareInstances) {
            if (!warnedOverflow) {
                warnedOverflow = true;
                spdlog::warn("[lens_flare] 要素が上限 {} を超えたので、超えた分は描きません", kMaxFlareInstances);
            }
            break;
        }
        instances[count++] = PackInstance(draw);
    }
    return count;
}

LensFlarePass::LensFlarePass() = default;
LensFlarePass::~LensFlarePass() = default;

bool LensFlarePass::Init(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap,
                         const rs::rhi::CommandQueue& queue, std::uint32_t firstSrvIndex) {
    impl_.reset();
    if (!device.Valid() || !srvHeap.Valid() || !queue.Valid()) return false;
    std::unique_ptr<Impl> impl = std::make_unique<Impl>();
    impl->device = device.Native();
    impl->queue = queue.Native();
    impl->srvHeap = srvHeap.Native();
    impl->srvStride = srvHeap.Stride();
    impl->firstSrvIndex = firstSrvIndex;
    if (!CreateLensFlarePipelines(impl->device, impl->rootSig, impl->elementPso, impl->whiteoutPso)) return false;
    if (!impl->CreateUploadBuffers()) return false;
    for (std::uint32_t slot = 0; slot < kMaxFlareTextures; ++slot) impl->WriteTextureSrv(slot, nullptr);
    impl_ = std::move(impl);
    return true;
}

bool LensFlarePass::Ready() const { return impl_ != nullptr; }

std::uint32_t LensFlarePass::DepthSrvIndex() const { return impl_ ? impl_->firstSrvIndex + kDepthSrvOffset : 0; }

bool LensFlarePass::UploadTexture(std::uint32_t slot, const ImageData& image) {
    if (!impl_ || slot >= kMaxFlareTextures || !image.ok) return false;
    ComPtr<ID3D12Resource> texture;
    if (!impl_->UploadTexture(image, texture)) return false;
    impl_->textures[slot] = std::move(texture);
    impl_->WriteTextureSrv(slot, impl_->textures[slot].Get());
    return true;
}

bool LensFlarePass::HasTexture(std::uint32_t slot) const {
    return impl_ && slot < kMaxFlareTextures && impl_->textures[slot] != nullptr;
}

void LensFlarePass::Record(const rs::rhi::CommandList& commandList, rs::rhi::CpuDescriptor rtv,
                           const LensFlareFrame& frame) const {
    if (!impl_ || !commandList.Valid() || !rtv.Valid() || !frame.HasWork() || frame.width == 0 || frame.height == 0)
        return;
    const std::uint32_t ring = frame.frameIndex % kRingSize;
    const std::uint32_t lightCount =
        static_cast<std::uint32_t>(std::min<std::size_t>(frame.lights.size(), kMaxFlareLights));
    if (frame.lights.size() > kMaxFlareLights && !impl_->warnedOverflow) {
        impl_->warnedOverflow = true;
        spdlog::warn("[lens_flare] 光源が上限 {} を超えたので、超えた分は使いません", kMaxFlareLights);
    }
    std::uint8_t* base = impl_->uploadMapped[ring];
    const std::uint32_t instanceCount = impl_->PackFrame(frame, base, lightCount);
    const bool drawWhiteout = frame.whiteout.enabled && frame.whiteout.lightIndex < lightCount;
    if (instanceCount == 0 && !drawWhiteout) return;

    FlareConstants constants = {};
    std::memcpy(constants.viewProj, frame.viewProj.m, sizeof(constants.viewProj));
    // 視点の行列(行ベクトルの規約)の列が、ワールドから見たカメラの右・上。
    constants.cameraRight[0] = frame.view.m[0];
    constants.cameraRight[1] = frame.view.m[4];
    constants.cameraRight[2] = frame.view.m[8];
    constants.cameraUp[0] = frame.view.m[1];
    constants.cameraUp[1] = frame.view.m[5];
    constants.cameraUp[2] = frame.view.m[9];
    constants.cameraPos[0] = frame.cameraPos.x;
    constants.cameraPos[1] = frame.cameraPos.y;
    constants.cameraPos[2] = frame.cameraPos.z;
    constants.sampleRadiusPx = kFlareVisibilitySampleRadiusPx;
    constants.depthToleranceRel = kFlareDepthToleranceRel;
    constants.screen[0] = static_cast<float>(frame.width);
    constants.screen[1] = static_cast<float>(frame.height);
    constants.whiteoutLight = frame.whiteout.lightIndex;
    CopyColor(constants.whiteoutColor, frame.whiteout.color);
    constants.flareHdrIntensity = frame.flareHdrIntensity;
    constants.whiteoutHdrLevel = frame.whiteoutHdrLevel;
    constants.sunAtmosphere = frame.sunAtmosphere ? 1u : 0u;

    ID3D12GraphicsCommandList* list = commandList.Native();
    const D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = {rtv.ptr};
    list->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
    const D3D12_VIEWPORT viewport = {0.0f, 0.0f, static_cast<float>(frame.width), static_cast<float>(frame.height),
                                     0.0f, 1.0f};
    const D3D12_RECT scissor = {0, 0, static_cast<LONG>(frame.width), static_cast<LONG>(frame.height)};
    list->RSSetViewports(1, &viewport);
    list->RSSetScissorRects(1, &scissor);
    ID3D12DescriptorHeap* heaps[] = {impl_->srvHeap};
    list->SetDescriptorHeaps(1, heaps);
    list->SetGraphicsRootSignature(impl_->rootSig.Get());
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    const D3D12_GPU_VIRTUAL_ADDRESS bufferAddress = impl_->uploadBuffers[ring]->GetGPUVirtualAddress();
    list->SetGraphicsRootShaderResourceView(kRootLights, bufferAddress);
    list->SetGraphicsRootShaderResourceView(kRootInstances, bufferAddress + kFlareInstanceOffset);
    list->SetGraphicsRootDescriptorTable(kRootDepthTable, impl_->GpuHandle(impl_->firstSrvIndex + kDepthSrvOffset));
    list->SetGraphicsRootDescriptorTable(kRootTextureTable,
                                         impl_->GpuHandle(impl_->firstSrvIndex + kFirstTextureSrvOffset));
    // 太陽に掛けないときも、テーブルは有効な枠を指しておく(シェーダは読まない)。
    list->SetGraphicsRootDescriptorTable(kRootTransmittanceTable, impl_->GpuHandle(frame.transmittanceSrvIndex));
    list->SetGraphicsRoot32BitConstants(kRootConstants, kFlareConstantDwords, &constants, 0);

    constexpr UINT kQuadVertexCount = 6;      // 板(三角形2つ)
    constexpr UINT kFullscreenVertexCount = 3;  // 全画面三角形
    if (instanceCount > 0) {
        list->SetPipelineState(impl_->elementPso.Get());
        list->DrawInstanced(kQuadVertexCount, instanceCount, 0, 0);
    }
    if (drawWhiteout) {
        list->SetPipelineState(impl_->whiteoutPso.Get());
        list->DrawInstanced(kFullscreenVertexCount, 1, 0, 0);
    }
}

} // namespace rs::render

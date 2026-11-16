#include "SceneHdrTarget.h"

#include <d3d12.h>
#include <wrl/client.h>

#include <DirectXPackedVector.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include <spdlog/spdlog.h>

#include "PostProcessFormats.h"
#include "TonemapMath.h"

using Microsoft::WRL::ComPtr;

namespace rs::render {

// シェーダが読む状態。ピクセルシェーダと compute の両方から読めるようにする。
constexpr D3D12_RESOURCE_STATES kShaderReadState =
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

struct SceneHdrTarget::Impl {
    ComPtr<ID3D12Device> device;
    rs::rhi::SrvHeap srvHeap;
    std::uint32_t srvIndex = 0;
    float clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    HdrTargetFormat format = HdrTargetFormat::Half16;

    ComPtr<ID3D12Resource> resource;
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    HdrTargetState state = HdrTargetState::ShaderResource;
    std::uint64_t srvGpuHandle = 0;

    // SetBackgroundGradient で指定した背景。転送元バッファは資源と同じ配置で、Resizeのたびに作り直す。
    bool hasGradient = false;
    float gradientTopSrgb[3] = {};
    float gradientBottomSrgb[3] = {};
    ComPtr<ID3D12Resource> background;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT backgroundFootprint = {};

    bool BuildBackground();
};

namespace {

D3D12_RESOURCE_STATES ToResourceState(HdrTargetState state) {
    switch (state) {
    case HdrTargetState::ShaderResource: return kShaderReadState;
    case HdrTargetState::RenderTarget: return D3D12_RESOURCE_STATE_RENDER_TARGET;
    case HdrTargetState::CopyDest: return D3D12_RESOURCE_STATE_COPY_DEST;
    case HdrTargetState::CopySource: return D3D12_RESOURCE_STATE_COPY_SOURCE;
    }
    return D3D12_RESOURCE_STATE_COMMON;
}

} // namespace

// 資源と同じ配置のUPLOADバッファを作り、上下のグラデーションを詰める。
// 見た目の色(sRGB)をsRGB空間で補間してから、リニアのhalf float(RGBA16F)へ変換する。
bool SceneHdrTarget::Impl::BuildBackground() {
    Impl& impl = *this;
    if (impl.format != HdrTargetFormat::Half16 || !impl.resource) return false;
    constexpr std::size_t kBytesPerPixel = sizeof(DirectX::PackedVector::HALF) * 4;
    const D3D12_RESOURCE_DESC resourceDesc = impl.resource->GetDesc();
    UINT64 totalBytes = 0;
    impl.device->GetCopyableFootprints(&resourceDesc, 0, 1, 0, &impl.backgroundFootprint, nullptr, nullptr,
                                       &totalBytes);

    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = totalBytes;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    impl.background.Reset();
    if (FAILED(impl.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                                                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                    IID_PPV_ARGS(&impl.background))))
        return false;

    std::uint8_t* mapped = nullptr;
    const D3D12_RANGE noRead = {0, 0};
    if (FAILED(impl.background->Map(0, &noRead, reinterpret_cast<void**>(&mapped)))) {
        spdlog::error("[scene_hdr] 背景バッファのMapに失敗しました");
        return false;
    }
    for (std::uint32_t y = 0; y < impl.height; ++y) {
        const float t = (static_cast<float>(y) + 0.5f) / static_cast<float>(impl.height);
        DirectX::PackedVector::HALF row[4] = {};
        for (int c = 0; c < 3; ++c) {
            const float srgb = std::lerp(impl.gradientTopSrgb[c], impl.gradientBottomSrgb[c], t);
            row[c] = DirectX::PackedVector::XMConvertFloatToHalf(SrgbToLinear(std::clamp(srgb, 0.0f, 1.0f)));
        }
        row[3] = DirectX::PackedVector::XMConvertFloatToHalf(1.0f);
        std::uint8_t* dst = mapped + impl.backgroundFootprint.Offset +
                            static_cast<std::size_t>(y) * impl.backgroundFootprint.Footprint.RowPitch;
        for (std::uint32_t x = 0; x < impl.width; ++x) std::memcpy(dst + x * kBytesPerPixel, row, sizeof(row));
    }
    impl.background->Unmap(0, nullptr);
    return true;
}

SceneHdrTarget::SceneHdrTarget() = default;
SceneHdrTarget::~SceneHdrTarget() = default;

bool SceneHdrTarget::Init(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap,
                          std::uint32_t srvIndex, const float clearColorLinear[3], HdrTargetFormat format) {
    impl_.reset();
    if (!device.Valid() || !srvHeap.Valid()) return false;
    std::unique_ptr<Impl> impl = std::make_unique<Impl>();
    impl->device = device.Native();
    impl->srvHeap = srvHeap;
    impl->srvIndex = srvIndex;
    impl->format = format;
    for (int i = 0; i < 3; ++i) impl->clearColor[i] = clearColorLinear[i];

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = 1;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    if (FAILED(impl->device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&impl->rtvHeap)))) {
        spdlog::error("[hdr_target] rtv heap");
        return false;
    }
    impl_ = std::move(impl);
    return true;
}

bool SceneHdrTarget::Resize(std::uint32_t width, std::uint32_t height) {
    if (!impl_ || width == 0 || height == 0) return false;
    Impl& impl = *impl_;
    impl.resource.Reset();
    impl.width = 0;
    impl.height = 0;

    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    DXGI_FORMAT dxgiFormat = kSceneHdrFormat;
    if (impl.format == HdrTargetFormat::Float32) dxgiFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;
    else if (impl.format == HdrTargetFormat::Srgb8) dxgiFormat = kTonemapOutputFormat;
    else if (impl.format == HdrTargetFormat::Unorm8) dxgiFormat = DXGI_FORMAT_R8_UNORM; // アンビエントオクルージョン(AmbientOcclusionPass の出力)
    else if (impl.format == HdrTargetFormat::Rg16Float) dxgiFormat = DXGI_FORMAT_R16G16_FLOAT; // 速度バッファ
    else if (impl.format == HdrTargetFormat::R32Float) dxgiFormat = DXGI_FORMAT_R32_FLOAT; // 測光パスの結果
    desc.Format = dxgiFormat;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = dxgiFormat;
    std::memcpy(clearValue.Color, impl.clearColor, sizeof(clearValue.Color));
    // 生成直後はトーンマップの入力(SRV)状態にしておく(WboitPass::CreateTargetsと同じ)。
    if (FAILED(impl.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                    kShaderReadState, &clearValue,
                                                    IID_PPV_ARGS(&impl.resource)))) {
        spdlog::error("[hdr_target] resource {}x{}", width, height);
        return false;
    }
    impl.state = HdrTargetState::ShaderResource;
    impl.width = width;
    impl.height = height;

    impl.device->CreateRenderTargetView(impl.resource.Get(), nullptr,
                                        impl.rtvHeap->GetCPUDescriptorHandleForHeapStart());

    ID3D12DescriptorHeap* srvHeap = impl.srvHeap.Native();
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = dxgiFormat;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    D3D12_CPU_DESCRIPTOR_HANDLE srvCpu = srvHeap->GetCPUDescriptorHandleForHeapStart();
    srvCpu.ptr += static_cast<SIZE_T>(impl.srvIndex) * impl.srvHeap.Stride();
    impl.device->CreateShaderResourceView(impl.resource.Get(), &srvDesc, srvCpu);
    D3D12_GPU_DESCRIPTOR_HANDLE srvGpu = srvHeap->GetGPUDescriptorHandleForHeapStart();
    srvGpu.ptr += static_cast<UINT64>(impl.srvIndex) * impl.srvHeap.Stride();
    impl.srvGpuHandle = srvGpu.ptr;
    impl.background.Reset();
    if (impl.hasGradient) return impl.BuildBackground();
    return true;
}

bool SceneHdrTarget::SetBackgroundGradient(const float topSrgb[3], const float bottomSrgb[3]) {
    if (!impl_) return false;
    impl_->hasGradient = true;
    for (int i = 0; i < 3; ++i) {
        impl_->gradientTopSrgb[i] = topSrgb[i];
        impl_->gradientBottomSrgb[i] = bottomSrgb[i];
    }
    return impl_->resource ? impl_->BuildBackground() : true; // 資源がまだなら、次の Resize が作る
}

void SceneHdrTarget::RecordCopyBackground(const rs::rhi::CommandList& commandList) {
    if (!Ready() || !impl_->background || !commandList.Valid()) return;
    Transition(commandList, HdrTargetState::CopyDest);
    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = impl_->background.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = impl_->backgroundFootprint;
    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = impl_->resource.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;
    commandList.Native()->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    Transition(commandList, HdrTargetState::RenderTarget);
}

bool SceneHdrTarget::RecordCopyTo(const rs::rhi::CommandList& commandList, void* destinationNative) {
    if (!Ready() || !commandList.Valid() || destinationNative == nullptr) return false;
    ID3D12Resource* destination = static_cast<ID3D12Resource*>(destinationNative);
    const D3D12_RESOURCE_DESC destinationDesc = destination->GetDesc();
    const D3D12_RESOURCE_DESC sourceDesc = impl_->resource->GetDesc();
    if (destinationDesc.Width != sourceDesc.Width || destinationDesc.Height != sourceDesc.Height ||
        destinationDesc.Format != sourceDesc.Format)
        return false;
    const HdrTargetState previous = impl_->state;
    Transition(commandList, HdrTargetState::CopySource);
    commandList.Native()->CopyResource(destination, impl_->resource.Get());
    Transition(commandList, previous);
    return true;
}

bool SceneHdrTarget::RecordCopyToBuffer(const rs::rhi::CommandList& commandList, void* bufferNative,
                                        std::uint32_t rowPitchBytes) {
    if (!Ready() || !commandList.Valid() || bufferNative == nullptr) return false;
    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = static_cast<ID3D12Resource*>(bufferNative);
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Footprint.Format = impl_->resource->GetDesc().Format;
    dst.PlacedFootprint.Footprint.Width = impl_->width;
    dst.PlacedFootprint.Footprint.Height = impl_->height;
    dst.PlacedFootprint.Footprint.Depth = 1;
    dst.PlacedFootprint.Footprint.RowPitch = rowPitchBytes;
    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = impl_->resource.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = 0;
    const HdrTargetState previous = impl_->state;
    Transition(commandList, HdrTargetState::CopySource);
    commandList.Native()->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    Transition(commandList, previous);
    return true;
}

bool SceneHdrTarget::SetClearColor(const float clearColorLinear[3]) {
    if (!impl_) return false;
    if (std::memcmp(impl_->clearColor, clearColorLinear, sizeof(float) * 3) == 0) return true;
    for (int i = 0; i < 3; ++i) impl_->clearColor[i] = clearColorLinear[i];
    if (!impl_->resource) return true; // まだ作っていなければ、次の Resize が新しい色で作る
    return Resize(impl_->width, impl_->height);
}

bool SceneHdrTarget::Ready() const { return impl_ && impl_->resource; }
std::uint32_t SceneHdrTarget::Width() const { return impl_ ? impl_->width : 0; }
std::uint32_t SceneHdrTarget::Height() const { return impl_ ? impl_->height : 0; }

void SceneHdrTarget::Transition(const rs::rhi::CommandList& commandList, HdrTargetState state) {
    if (!Ready() || !commandList.Valid() || impl_->state == state) return;
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = impl_->resource.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = ToResourceState(impl_->state);
    barrier.Transition.StateAfter = ToResourceState(state);
    commandList.Native()->ResourceBarrier(1, &barrier);
    impl_->state = state;
}

void SceneHdrTarget::ForceState(HdrTargetState state) {
    if (impl_) impl_->state = state;
}

void SceneHdrTarget::BeginScene(const rs::rhi::CommandList& commandList, rs::rhi::CpuDescriptor depthDsv) {
    if (!Ready() || !commandList.Valid()) return;
    Transition(commandList, HdrTargetState::RenderTarget);
    const D3D12_CPU_DESCRIPTOR_HANDLE rtv = impl_->rtvHeap->GetCPUDescriptorHandleForHeapStart();
    const D3D12_CPU_DESCRIPTOR_HANDLE dsv = {depthDsv.ptr};
    commandList.Native()->OMSetRenderTargets(1, &rtv, FALSE, depthDsv.Valid() ? &dsv : nullptr);
    commandList.Native()->ClearRenderTargetView(rtv, impl_->clearColor, 0, nullptr);
}

rs::rhi::CpuDescriptor SceneHdrTarget::Rtv() const {
    if (!impl_ || !impl_->rtvHeap) return {};
    return {impl_->rtvHeap->GetCPUDescriptorHandleForHeapStart().ptr};
}

const rs::rhi::SrvHeap& SceneHdrTarget::SrvHeapRef() const {
    static const rs::rhi::SrvHeap kEmpty;
    return impl_ ? impl_->srvHeap : kEmpty;
}

std::uint64_t SceneHdrTarget::SrvGpuHandle() const { return impl_ ? impl_->srvGpuHandle : 0; }

std::uint32_t SceneHdrTarget::SrvIndex() const { return impl_ ? impl_->srvIndex : 0; }

bool SceneHdrTarget::RecordCopyFrom(const rs::rhi::CommandList& commandList, SceneHdrTarget& source) {
    if (!Ready() || !source.Ready() || !commandList.Valid()) return false;
    if (impl_->width != source.impl_->width || impl_->height != source.impl_->height ||
        impl_->format != source.impl_->format)
        return false;
    const HdrTargetState sourcePrevious = source.impl_->state;
    Transition(commandList, HdrTargetState::CopyDest);
    source.Transition(commandList, HdrTargetState::CopySource);
    commandList.Native()->CopyResource(impl_->resource.Get(), source.impl_->resource.Get());
    Transition(commandList, HdrTargetState::ShaderResource);
    source.Transition(commandList, sourcePrevious);
    return true;
}

} // namespace rs::render

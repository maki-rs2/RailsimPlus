#include "ShadowMap.h"

#include <d3d12.h>
#include <wrl/client.h>

#include <utility>

#include <spdlog/spdlog.h>

using Microsoft::WRL::ComPtr;

namespace rs::render {

// シェーダが読む状態。ピクセルシェーダと compute の両方から読めるようにする。
constexpr D3D12_RESOURCE_STATES kShaderReadState =
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

namespace {

constexpr DXGI_FORMAT kShadowResourceFormat = DXGI_FORMAT_R32_TYPELESS;
constexpr DXGI_FORMAT kShadowDepthFormat = DXGI_FORMAT_D32_FLOAT;
constexpr DXGI_FORMAT kShadowReadFormat = DXGI_FORMAT_R32_FLOAT;
// 消去値。通常方向の深度なので、遠(=何も無い)の1.0。資源の最適化クリア値と一致させる。
constexpr float kShadowClearDepth = 1.0f;

} // namespace

struct ShadowMap::Impl {
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12Resource> resource;
    ComPtr<ID3D12DescriptorHeap> dsvHeap;
    UINT dsvStride = 0;
    std::uint32_t size = 0;
    std::uint32_t sliceCount = 0;
    std::uint32_t srvIndex = 0;
    ShadowMapState state = ShadowMapState::ShaderResource;
};

ShadowMap::ShadowMap() = default;
ShadowMap::~ShadowMap() = default;

bool ShadowMap::Init(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap,
                     std::uint32_t srvIndex, std::uint32_t size, std::uint32_t sliceCount) {
    impl_.reset();
    if (!device.Valid() || !srvHeap.Valid() || size == 0 || sliceCount == 0) return false;
    std::unique_ptr<Impl> impl = std::make_unique<Impl>();
    impl->device = device.Native();
    impl->size = size;
    impl->sliceCount = sliceCount;
    impl->srvIndex = srvIndex;

    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = size;
    desc.Height = size;
    desc.DepthOrArraySize = static_cast<UINT16>(sliceCount);
    desc.MipLevels = 1;
    desc.Format = kShadowResourceFormat;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE clear = {};
    clear.Format = kShadowDepthFormat;
    clear.DepthStencil.Depth = kShadowClearDepth;
    if (FAILED(impl->device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                     kShaderReadState, &clear,
                                                     IID_PPV_ARGS(&impl->resource)))) {
        spdlog::error("[shadow_map] resource");
        return false;
    }

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = sliceCount;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    if (FAILED(impl->device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&impl->dsvHeap)))) {
        spdlog::error("[shadow_map] dsv heap");
        return false;
    }
    impl->dsvStride = impl->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    for (std::uint32_t slice = 0; slice < sliceCount; ++slice) {
        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format = kShadowDepthFormat;
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
        dsvDesc.Texture2DArray.FirstArraySlice = slice;
        dsvDesc.Texture2DArray.ArraySize = 1;
        D3D12_CPU_DESCRIPTOR_HANDLE dsvCpu = impl->dsvHeap->GetCPUDescriptorHandleForHeapStart();
        dsvCpu.ptr += static_cast<SIZE_T>(slice) * impl->dsvStride;
        impl->device->CreateDepthStencilView(impl->resource.Get(), &dsvDesc, dsvCpu);
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = kShadowReadFormat;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
    srvDesc.Texture2DArray.MipLevels = 1;
    srvDesc.Texture2DArray.ArraySize = sliceCount;
    D3D12_CPU_DESCRIPTOR_HANDLE srvCpu = srvHeap.Native()->GetCPUDescriptorHandleForHeapStart();
    srvCpu.ptr += static_cast<SIZE_T>(srvIndex) * srvHeap.Stride();
    impl->device->CreateShaderResourceView(impl->resource.Get(), &srvDesc, srvCpu);

    impl_ = std::move(impl);
    return true;
}

bool ShadowMap::Ready() const { return impl_ && impl_->resource; }
std::uint32_t ShadowMap::Size() const { return impl_ ? impl_->size : 0; }
std::uint32_t ShadowMap::SliceCount() const { return impl_ ? impl_->sliceCount : 0; }
std::uint32_t ShadowMap::SrvIndex() const { return impl_ ? impl_->srvIndex : 0; }

void ShadowMap::Transition(const rs::rhi::CommandList& commandList, ShadowMapState state) {
    if (!Ready() || !commandList.Valid() || impl_->state == state) return;
    const bool toDepthWrite = state == ShadowMapState::DepthWrite;
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = impl_->resource.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = toDepthWrite ? kShaderReadState
                                                  : D3D12_RESOURCE_STATE_DEPTH_WRITE;
    barrier.Transition.StateAfter = toDepthWrite ? D3D12_RESOURCE_STATE_DEPTH_WRITE
                                                 : kShaderReadState;
    commandList.Native()->ResourceBarrier(1, &barrier);
    impl_->state = state;
}

void ShadowMap::BeginSlice(const rs::rhi::CommandList& commandList, std::uint32_t slice) const {
    if (!Ready() || !commandList.Valid() || slice >= impl_->sliceCount) return;
    ID3D12GraphicsCommandList* list = commandList.Native();
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = impl_->dsvHeap->GetCPUDescriptorHandleForHeapStart();
    dsv.ptr += static_cast<SIZE_T>(slice) * impl_->dsvStride;
    list->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, kShadowClearDepth, 0, 0, nullptr);
    list->OMSetRenderTargets(0, nullptr, FALSE, &dsv);
    const D3D12_VIEWPORT viewport = {0.0f, 0.0f, static_cast<float>(impl_->size),
                                     static_cast<float>(impl_->size), 0.0f, 1.0f};
    const D3D12_RECT scissor = {0, 0, static_cast<LONG>(impl_->size), static_cast<LONG>(impl_->size)};
    list->RSSetViewports(1, &viewport);
    list->RSSetScissorRects(1, &scissor);
}

} // namespace rs::render

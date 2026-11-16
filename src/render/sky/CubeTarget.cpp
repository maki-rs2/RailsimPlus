#include "CubeTarget.h"

#include <d3d12.h>
#include <wrl/client.h>

#include <algorithm>

#include <spdlog/spdlog.h>

#include "../postprocess/PostProcessFormats.h"
#include "IblParams.h"

using Microsoft::WRL::ComPtr;

namespace rs::render {

// シェーダが読む状態。ピクセルシェーダと compute の両方から読めるようにする。
constexpr D3D12_RESOURCE_STATES kShaderReadState =
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

struct CubeTarget::Impl {
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12Resource> resource;
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    UINT rtvStride = 0;
    std::uint32_t size = 0;
    std::uint32_t mipCount = 0;
    DXGI_FORMAT format = kSceneHdrFormat;
    HdrTargetState state = HdrTargetState::ShaderResource;
    std::uint64_t srvGpuHandle = 0;
};

namespace {

// RTVヒープ内の位置(ミップの外側ループ、面の内側ループ)。
UINT RtvIndex(std::uint32_t face, std::uint32_t mip) { return mip * ibl::kIblCubeFaceCount + face; }

} // namespace

CubeTarget::CubeTarget() = default;
CubeTarget::~CubeTarget() = default;

bool CubeTarget::Init(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap, std::uint32_t srvIndex,
                      std::uint32_t size, std::uint32_t mipCount, CubeFormat format) {
    impl_.reset();
    if (!device.Valid() || !srvHeap.Valid() || size == 0 || mipCount == 0) return false;
    std::unique_ptr<Impl> impl = std::make_unique<Impl>();
    impl->device = device.Native();
    impl->size = size;
    impl->mipCount = mipCount;
    impl->format = format == CubeFormat::Float32 ? DXGI_FORMAT_R32G32B32A32_FLOAT : kSceneHdrFormat;

    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = size;
    desc.Height = size;
    desc.DepthOrArraySize = ibl::kIblCubeFaceCount;
    desc.MipLevels = static_cast<UINT16>(mipCount);
    desc.Format = impl->format;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    // 毎回全面を描くのでクリア値は使わない。生成直後はSRV状態にしておく(SceneHdrTargetと同じ)。
    if (FAILED(impl->device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                     kShaderReadState, nullptr,
                                                     IID_PPV_ARGS(&impl->resource)))) {
        spdlog::error("[cube_target] resource {}x{}x6 mip{}", size, size, mipCount);
        return false;
    }

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = ibl::kIblCubeFaceCount * mipCount;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    if (FAILED(impl->device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&impl->rtvHeap)))) {
        spdlog::error("[cube_target] rtv heap");
        return false;
    }
    impl->rtvStride = impl->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    for (std::uint32_t mip = 0; mip < mipCount; ++mip) {
        for (std::uint32_t face = 0; face < ibl::kIblCubeFaceCount; ++face) {
            D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
            rtvDesc.Format = impl->format;
            rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
            rtvDesc.Texture2DArray.MipSlice = mip;
            rtvDesc.Texture2DArray.FirstArraySlice = face;
            rtvDesc.Texture2DArray.ArraySize = 1;
            D3D12_CPU_DESCRIPTOR_HANDLE rtv = impl->rtvHeap->GetCPUDescriptorHandleForHeapStart();
            rtv.ptr += static_cast<SIZE_T>(RtvIndex(face, mip)) * impl->rtvStride;
            impl->device->CreateRenderTargetView(impl->resource.Get(), &rtvDesc, rtv);
        }
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = impl->format;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
    srvDesc.TextureCube.MostDetailedMip = 0;
    srvDesc.TextureCube.MipLevels = mipCount;
    ID3D12DescriptorHeap* srvHeapNative = srvHeap.Native();
    D3D12_CPU_DESCRIPTOR_HANDLE srvCpu = srvHeapNative->GetCPUDescriptorHandleForHeapStart();
    srvCpu.ptr += static_cast<SIZE_T>(srvIndex) * srvHeap.Stride();
    impl->device->CreateShaderResourceView(impl->resource.Get(), &srvDesc, srvCpu);
    D3D12_GPU_DESCRIPTOR_HANDLE srvGpu = srvHeapNative->GetGPUDescriptorHandleForHeapStart();
    srvGpu.ptr += static_cast<UINT64>(srvIndex) * srvHeap.Stride();
    impl->srvGpuHandle = srvGpu.ptr;

    impl_ = std::move(impl);
    return true;
}

bool CubeTarget::Ready() const { return impl_ && impl_->resource; }
std::uint32_t CubeTarget::MipCount() const { return impl_ ? impl_->mipCount : 0; }

std::uint32_t CubeTarget::MipSize(std::uint32_t mip) const {
    return impl_ ? std::max<std::uint32_t>(1, impl_->size >> mip) : 0;
}

void CubeTarget::Transition(const rs::rhi::CommandList& commandList, HdrTargetState state) {
    if (!Ready() || !commandList.Valid() || impl_->state == state) return;
    const bool toRenderTarget = state == HdrTargetState::RenderTarget;
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = impl_->resource.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore =
        toRenderTarget ? kShaderReadState : D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter =
        toRenderTarget ? D3D12_RESOURCE_STATE_RENDER_TARGET : kShaderReadState;
    commandList.Native()->ResourceBarrier(1, &barrier);
    impl_->state = state;
}

void CubeTarget::TransitionMip(const rs::rhi::CommandList& commandList, std::uint32_t mip, HdrTargetState state) {
    if (!Ready() || !commandList.Valid() || mip >= impl_->mipCount) return;
    const bool toRenderTarget = state == HdrTargetState::RenderTarget;
    D3D12_RESOURCE_BARRIER barriers[ibl::kIblCubeFaceCount] = {};
    for (std::uint32_t face = 0; face < ibl::kIblCubeFaceCount; ++face) {
        D3D12_RESOURCE_BARRIER& barrier = barriers[face];
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = impl_->resource.Get();
        // サブリソース番号 = ミップ + 配列スライス(面) × ミップ数。
        barrier.Transition.Subresource = mip + face * impl_->mipCount;
        barrier.Transition.StateBefore =
            toRenderTarget ? kShaderReadState : D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter =
            toRenderTarget ? D3D12_RESOURCE_STATE_RENDER_TARGET : kShaderReadState;
    }
    commandList.Native()->ResourceBarrier(ibl::kIblCubeFaceCount, barriers);
}

void CubeTarget::MarkAllMips(HdrTargetState state) {
    if (Ready()) impl_->state = state;
}

void CubeTarget::BindFaceMip(const rs::rhi::CommandList& commandList, std::uint32_t face,
                             std::uint32_t mip) const {
    if (!Ready() || !commandList.Valid() || face >= ibl::kIblCubeFaceCount || mip >= impl_->mipCount) return;
    ID3D12GraphicsCommandList* list = commandList.Native();
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = impl_->rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += static_cast<SIZE_T>(RtvIndex(face, mip)) * impl_->rtvStride;
    list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    const std::uint32_t mipSize = MipSize(mip);
    const D3D12_VIEWPORT viewport = {0.0f, 0.0f, static_cast<float>(mipSize), static_cast<float>(mipSize), 0.0f, 1.0f};
    const D3D12_RECT scissor = {0, 0, static_cast<LONG>(mipSize), static_cast<LONG>(mipSize)};
    list->RSSetViewports(1, &viewport);
    list->RSSetScissorRects(1, &scissor);
}

std::uint64_t CubeTarget::SrvGpuHandle() const { return impl_ ? impl_->srvGpuHandle : 0; }

} // namespace rs::render

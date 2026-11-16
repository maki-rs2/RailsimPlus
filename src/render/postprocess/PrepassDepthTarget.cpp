#include "PrepassDepthTarget.h"

#include <d3d12.h>
#include <wrl/client.h>

#include <spdlog/spdlog.h>

#include "../pipeline/MeshPipelineTypes.h"

using Microsoft::WRL::ComPtr;

namespace rs::render {

struct PrepassDepthTarget::Impl {
    ComPtr<ID3D12Device> device;
    rs::rhi::SrvHeap srvHeap;
    std::uint32_t srvIndex = 0;
    std::uint64_t srvGpuHandle = 0;
    bool readable = false; // 今 ShaderResource 状態か(偽なら DepthWrite)
    ComPtr<ID3D12Resource> resource;
    ComPtr<ID3D12DescriptorHeap> dsvHeap;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

PrepassDepthTarget::PrepassDepthTarget() = default;
PrepassDepthTarget::~PrepassDepthTarget() = default;

bool PrepassDepthTarget::Init(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap,
                              std::uint32_t srvIndex) {
    impl_.reset();
    if (!device.Valid() || !srvHeap.Valid()) return false;
    std::unique_ptr<Impl> impl = std::make_unique<Impl>();
    impl->device = device.Native();
    impl->srvHeap = srvHeap;
    impl->srvIndex = srvIndex;
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = 1;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    if (FAILED(impl->device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&impl->dsvHeap)))) {
        spdlog::error("[prepass_depth] dsv heap");
        return false;
    }
    impl_ = std::move(impl);
    return true;
}

bool PrepassDepthTarget::Resize(std::uint32_t width, std::uint32_t height) {
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
    desc.Format = DXGI_FORMAT_R32_TYPELESS;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE clear = {};
    clear.Format = DXGI_FORMAT_D32_FLOAT;
    clear.DepthStencil.Depth = kReversedZClearDepth;
    if (FAILED(impl.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                    D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear,
                                                    IID_PPV_ARGS(&impl.resource)))) {
        spdlog::error("[prepass_depth] resource {}x{}", width, height);
        return false;
    }
    D3D12_DEPTH_STENCIL_VIEW_DESC view = {};
    view.Format = DXGI_FORMAT_D32_FLOAT;
    view.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    impl.device->CreateDepthStencilView(impl.resource.Get(), &view, impl.dsvHeap->GetCPUDescriptorHandleForHeapStart());
    impl.readable = false;

    ID3D12DescriptorHeap* srvHeap = impl.srvHeap.Native();
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    D3D12_CPU_DESCRIPTOR_HANDLE srvCpu = srvHeap->GetCPUDescriptorHandleForHeapStart();
    srvCpu.ptr += static_cast<SIZE_T>(impl.srvIndex) * impl.srvHeap.Stride();
    impl.device->CreateShaderResourceView(impl.resource.Get(), &srvDesc, srvCpu);
    D3D12_GPU_DESCRIPTOR_HANDLE srvGpu = srvHeap->GetGPUDescriptorHandleForHeapStart();
    srvGpu.ptr += static_cast<UINT64>(impl.srvIndex) * impl.srvHeap.Stride();
    impl.srvGpuHandle = srvGpu.ptr;
    impl.width = width;
    impl.height = height;
    return true;
}

bool PrepassDepthTarget::Ready() const { return impl_ && impl_->resource; }
std::uint32_t PrepassDepthTarget::Width() const { return impl_ ? impl_->width : 0; }
std::uint32_t PrepassDepthTarget::Height() const { return impl_ ? impl_->height : 0; }

namespace {

void RecordTransition(ID3D12GraphicsCommandList* list, ID3D12Resource* resource, D3D12_RESOURCE_STATES before,
                      D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    list->ResourceBarrier(1, &barrier);
}

} // namespace

void PrepassDepthTarget::Begin(const rs::rhi::CommandList& commandList) {
    if (!Ready() || !commandList.Valid()) return;
    const D3D12_CPU_DESCRIPTOR_HANDLE dsv = impl_->dsvHeap->GetCPUDescriptorHandleForHeapStart();
    ID3D12GraphicsCommandList* list = commandList.Native();
    if (impl_->readable) {
        RecordTransition(list, impl_->resource.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                         D3D12_RESOURCE_STATE_DEPTH_WRITE);
        impl_->readable = false;
    }
    list->OMSetRenderTargets(0, nullptr, FALSE, &dsv);
    list->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, kReversedZClearDepth, 0, 0, nullptr);
}

void PrepassDepthTarget::BindWithColor(const rs::rhi::CommandList& commandList, rs::rhi::CpuDescriptor rtv) {
    if (!Ready() || !commandList.Valid() || impl_->readable || !rtv.Valid()) return;
    const D3D12_CPU_DESCRIPTOR_HANDLE color = {rtv.ptr};
    const D3D12_CPU_DESCRIPTOR_HANDLE dsv = impl_->dsvHeap->GetCPUDescriptorHandleForHeapStart();
    commandList.Native()->OMSetRenderTargets(1, &color, FALSE, &dsv);
}

void PrepassDepthTarget::EndWrite(const rs::rhi::CommandList& commandList) {
    if (!Ready() || !commandList.Valid() || impl_->readable) return;
    RecordTransition(commandList.Native(), impl_->resource.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE,
                     D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    impl_->readable = true;
}

std::uint32_t PrepassDepthTarget::SrvIndex() const { return impl_ ? impl_->srvIndex : 0; }
std::uint64_t PrepassDepthTarget::SrvGpuHandle() const { return impl_ ? impl_->srvGpuHandle : 0; }

} // namespace rs::render

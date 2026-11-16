#include "VolumeTexture.h"

#include <d3d12.h>
#include <wrl/client.h>

#include <spdlog/spdlog.h>

using Microsoft::WRL::ComPtr;

namespace rs::render {

namespace {

constexpr DXGI_FORMAT kFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
constexpr std::uint32_t kBytesPerTexel = 8;
constexpr D3D12_RESOURCE_STATES kReadState =
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

D3D12_RESOURCE_STATES NativeState(VolumeTextureState state) {
    return state == VolumeTextureState::UnorderedAccess ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : kReadState;
}

} // namespace

struct VolumeTexture::Impl {
    ID3D12Device* device = nullptr;
    rs::rhi::SrvHeap srvHeap;
    std::uint32_t firstSrvIndex = 0;
    ComPtr<ID3D12Resource> resource;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t depth = 0;
    VolumeTextureState state = VolumeTextureState::ShaderResource;

    D3D12_CPU_DESCRIPTOR_HANDLE Cpu(std::uint32_t slot) const {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = srvHeap.Native()->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(firstSrvIndex + slot) * srvHeap.Stride();
        return handle;
    }
};

VolumeTexture::VolumeTexture() = default;
VolumeTexture::~VolumeTexture() = default;

bool VolumeTexture::Init(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap, std::uint32_t firstSrvIndex) {
    impl_.reset();
    if (!device.Valid() || !srvHeap.Valid()) return false;
    impl_ = std::make_unique<Impl>();
    impl_->device = device.Native();
    impl_->srvHeap = srvHeap;
    impl_->firstSrvIndex = firstSrvIndex;
    return true;
}

bool VolumeTexture::Resize(std::uint32_t width, std::uint32_t height, std::uint32_t depth) {
    if (!impl_ || width == 0 || height == 0 || depth == 0) return false;
    Impl& impl = *impl_;
    impl.resource.Reset();
    impl.width = impl.height = impl.depth = 0;

    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = static_cast<UINT16>(depth);
    desc.MipLevels = 1;
    desc.Format = kFormat;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (FAILED(impl.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, kReadState, nullptr,
                                                    IID_PPV_ARGS(&impl.resource)))) {
        spdlog::error("[volume_texture] 資源を作れませんでした {}x{}x{}", width, height, depth);
        return false;
    }
    impl.state = VolumeTextureState::ShaderResource;

    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Format = kFormat;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture3D.MipLevels = 1;
    impl.device->CreateShaderResourceView(impl.resource.Get(), &srv, impl.Cpu(0));

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
    uav.Format = kFormat;
    uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
    uav.Texture3D.WSize = depth;
    impl.device->CreateUnorderedAccessView(impl.resource.Get(), nullptr, &uav, impl.Cpu(1));

    impl.width = width;
    impl.height = height;
    impl.depth = depth;
    return true;
}

bool VolumeTexture::Ready() const { return impl_ && impl_->resource; }
std::uint32_t VolumeTexture::Width() const { return impl_ ? impl_->width : 0; }
std::uint32_t VolumeTexture::Height() const { return impl_ ? impl_->height : 0; }
std::uint32_t VolumeTexture::Depth() const { return impl_ ? impl_->depth : 0; }
std::uint32_t VolumeTexture::SrvIndex() const { return impl_ ? impl_->firstSrvIndex : 0; }
std::uint32_t VolumeTexture::UavIndex() const { return impl_ ? impl_->firstSrvIndex + 1 : 0; }

void VolumeTexture::Transition(const rs::rhi::CommandList& commandList, VolumeTextureState state) {
    if (!Ready() || !commandList.Valid() || impl_->state == state) return;
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = impl_->resource.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = NativeState(impl_->state);
    barrier.Transition.StateAfter = NativeState(state);
    commandList.Native()->ResourceBarrier(1, &barrier);
    impl_->state = state;
}

void VolumeTexture::UavBarrier(const rs::rhi::CommandList& commandList) const {
    if (!Ready() || !commandList.Valid()) return;
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = impl_->resource.Get();
    commandList.Native()->ResourceBarrier(1, &barrier);
}

std::uint32_t VolumeTexture::ReadbackRowPitch() const {
    const std::uint32_t bytes = Width() * kBytesPerTexel;
    return (bytes + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) / D3D12_TEXTURE_DATA_PITCH_ALIGNMENT *
           D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
}

std::uint64_t VolumeTexture::ReadbackByteSize() const {
    // スライスは「行のバイト数×高さ」ごとに並ぶ。
    return static_cast<std::uint64_t>(ReadbackRowPitch()) * Height() * Depth();
}

bool VolumeTexture::RecordCopyToBuffer(const rs::rhi::CommandList& commandList, void* destBuffer) {
    if (!Ready() || !commandList.Valid() || !destBuffer) return false;
    Impl& impl = *impl_;
    ID3D12GraphicsCommandList* list = commandList.Native();
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = impl.resource.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = NativeState(impl.state);
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    list->ResourceBarrier(1, &barrier);

    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = impl.resource.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = static_cast<ID3D12Resource*>(destBuffer);
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Footprint.Format = kFormat;
    dst.PlacedFootprint.Footprint.Width = impl.width;
    dst.PlacedFootprint.Footprint.Height = impl.height;
    dst.PlacedFootprint.Footprint.Depth = impl.depth;
    dst.PlacedFootprint.Footprint.RowPitch = ReadbackRowPitch();
    list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barrier.Transition.StateAfter = kReadState;
    list->ResourceBarrier(1, &barrier);
    impl.state = VolumeTextureState::ShaderResource;
    return true;
}

} // namespace rs::render

#include "DepthProbe.h"

#include <d3d12.h>
#include <wrl/client.h>

#include <utility>

#include <spdlog/spdlog.h>

using Microsoft::WRL::ComPtr;

namespace rs::render {

namespace {
// 写してから取り出すまでに待つ Poll の回数(コマンドの実行が終わるまでのフレーム数の余裕)。
constexpr int kReadbackDelayPolls = 4;
constexpr UINT kReadbackBufferBytes = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
} // namespace

struct DepthProbe::Impl {
    ComPtr<ID3D12Resource> readback;
    int pollsLeft = 0; // 0=読み戻し待ちなし
};

DepthProbe::DepthProbe() : impl_(std::make_unique<Impl>()) {}
DepthProbe::~DepthProbe() = default;
DepthProbe::DepthProbe(DepthProbe&&) noexcept = default;
DepthProbe& DepthProbe::operator=(DepthProbe&&) noexcept = default;

bool DepthProbe::Record(const rs::rhi::CommandList& commandList, void* nativeDepth, unsigned x, unsigned y) {
    ID3D12Resource* const depth = static_cast<ID3D12Resource*>(nativeDepth);
    if (!commandList.Valid() || depth == nullptr || impl_->pollsLeft > 0) return false;
    if (!impl_->readback) {
        ComPtr<ID3D12Device> device;
        if (FAILED(depth->GetDevice(IID_PPV_ARGS(&device)))) return false;
        D3D12_HEAP_PROPERTIES heap = {};
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = kReadbackBufferBytes;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
                                                   nullptr, IID_PPV_ARGS(&impl_->readback))))
            return false;
    }
    ID3D12GraphicsCommandList* list = commandList.Native();
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = depth;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    list->ResourceBarrier(1, &barrier);

    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = impl_->readback.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R32_FLOAT;
    dst.PlacedFootprint.Footprint.Width = 1;
    dst.PlacedFootprint.Footprint.Height = 1;
    dst.PlacedFootprint.Footprint.Depth = 1;
    dst.PlacedFootprint.Footprint.RowPitch = kReadbackBufferBytes;
    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = depth;
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = 0;
    const D3D12_BOX box = {x, y, 0, x + 1, y + 1, 1};
    list->CopyTextureRegion(&dst, 0, 0, 0, &src, &box);

    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    list->ResourceBarrier(1, &barrier);
    impl_->pollsLeft = kReadbackDelayPolls;
    return true;
}

bool DepthProbe::Poll(float& depth) {
    if (impl_->pollsLeft <= 0 || !impl_->readback) return false;
    if (--impl_->pollsLeft > 0) return false;
    float* data = nullptr;
    const D3D12_RANGE range = {0, sizeof(float)};
    if (FAILED(impl_->readback->Map(0, &range, reinterpret_cast<void**>(&data)))) {
        spdlog::error("[depth_probe] 読み戻しバッファの Map に失敗しました");
        return false;
    }
    depth = data[0];
    const D3D12_RANGE written = {0, 0};
    impl_->readback->Unmap(0, &written);
    return true;
}

} // namespace rs::render

#include "ExposureTarget.h"

#include "../../platform/EnvironmentVariable.h"
#include <d3d12.h>
#include <wrl/client.h>

#include <cstdlib>
#include <cstring>
#include <utility>

#include <spdlog/spdlog.h>

using Microsoft::WRL::ComPtr;

namespace rs::render {

// シェーダが読む状態。ピクセルシェーダと compute の両方から読めるようにする。
constexpr D3D12_RESOURCE_STATES kShaderReadState =
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

namespace {

constexpr DXGI_FORMAT kExposureFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;
// 読み戻しを取り出すまでに待つ Poll の回数(フレーム数。記録したコマンドが終わるまでの余裕)。
constexpr int kReadbackDelayPolls = 4;
// テクスチャ→バッファのコピーの行ピッチの最小単位。
constexpr UINT kReadbackBufferBytes = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;

} // namespace

struct ExposureTarget::Impl {
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12Resource> resource;
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    HdrTargetState state = HdrTargetState::ShaderResource;
    std::uint64_t srvGpuHandle = 0;
    ComPtr<ID3D12Resource> readback;
    int readbackPollsLeft = 0; // 0=読み戻し待ちなし
};

ExposureTarget::ExposureTarget() = default;
ExposureTarget::~ExposureTarget() = default;

bool ExposureTarget::Init(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap, std::uint32_t srvIndex) {
    impl_.reset();
    if (!device.Valid() || !srvHeap.Valid()) return false;
    std::unique_ptr<Impl> impl = std::make_unique<Impl>();
    impl->device = device.Native();

    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = kPixelCount;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = kExposureFormat;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    if (FAILED(impl->device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                     kShaderReadState, nullptr,
                                                     IID_PPV_ARGS(&impl->resource)))) {
        spdlog::error("[exposure_target] resource");
        return false;
    }

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = 1;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    if (FAILED(impl->device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&impl->rtvHeap)))) {
        spdlog::error("[exposure_target] rtv heap");
        return false;
    }
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = kExposureFormat;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    impl->device->CreateRenderTargetView(impl->resource.Get(), &rtvDesc,
                                         impl->rtvHeap->GetCPUDescriptorHandleForHeapStart());

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = kExposureFormat;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
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

bool ExposureTarget::Ready() const { return impl_ && impl_->resource; }

void ExposureTarget::Transition(const rs::rhi::CommandList& commandList, HdrTargetState state) {
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

void ExposureTarget::Bind(const rs::rhi::CommandList& commandList, std::uint32_t pixel) const {
    if (!Ready() || !commandList.Valid() || pixel >= kPixelCount) return;
    ID3D12GraphicsCommandList* list = commandList.Native();
    const D3D12_CPU_DESCRIPTOR_HANDLE rtv = impl_->rtvHeap->GetCPUDescriptorHandleForHeapStart();
    list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    const D3D12_VIEWPORT viewport = {static_cast<float>(pixel), 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
    const D3D12_RECT scissor = {static_cast<LONG>(pixel), 0, static_cast<LONG>(pixel) + 1, 1};
    list->RSSetViewports(1, &viewport);
    list->RSSetScissorRects(1, &scissor);
}

std::uint64_t ExposureTarget::SrvGpuHandle() const { return impl_ ? impl_->srvGpuHandle : 0; }

bool ExposureTarget::DebugReadbackEnabled() {
    static const bool kEnabled = []() {
        const char* value = rs::platform::ReadEnvironmentVariable("RS_DEBUG_ADAPTATION");
        return value != nullptr && value[0] == '1';
    }();
    return kEnabled;
}

void ExposureTarget::RecordReadback(const rs::rhi::CommandList& commandList) {
    if (!DebugReadbackEnabled() || !Ready() || !commandList.Valid() || impl_->state != HdrTargetState::ShaderResource)
        return;
    if (!impl_->readback) {
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
        if (FAILED(impl_->device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                          D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                          IID_PPV_ARGS(&impl_->readback))))
            return;
    }
    ID3D12GraphicsCommandList* list = commandList.Native();
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = impl_->resource.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = kShaderReadState;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    list->ResourceBarrier(1, &barrier);

    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = impl_->readback.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Footprint.Format = kExposureFormat;
    dst.PlacedFootprint.Footprint.Width = kPixelCount;
    dst.PlacedFootprint.Footprint.Height = 1;
    dst.PlacedFootprint.Footprint.Depth = 1;
    dst.PlacedFootprint.Footprint.RowPitch = kReadbackBufferBytes;
    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = impl_->resource.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = 0;
    list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    list->ResourceBarrier(1, &barrier);
    impl_->readbackPollsLeft = kReadbackDelayPolls;
}

bool ExposureTarget::PollReadback(float& gain, float& luminance) {
    if (!impl_ || impl_->readbackPollsLeft <= 0 || !impl_->readback) return false;
    if (--impl_->readbackPollsLeft > 0) return false;
    float* data = nullptr;
    const D3D12_RANGE range = {0, 2 * sizeof(float)};
    if (FAILED(impl_->readback->Map(0, &range, reinterpret_cast<void**>(&data)))) {
        spdlog::error("[exposure] 読み戻しバッファのMapに失敗しました");
        return false;
    }
    gain = data[0];
    luminance = data[1];
    const D3D12_RANGE written = {0, 0};
    impl_->readback->Unmap(0, &written);
    return true;
}

} // namespace rs::render

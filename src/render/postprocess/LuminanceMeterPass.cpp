#include "LuminanceMeterPass.h"

#include <d3d12.h>
#include <wrl/client.h>

#include <cstring>
#include <utility>

#include <spdlog/spdlog.h>

#include "FullscreenPassPipelines.h"
#include "LuminanceMeterShader.h"

using Microsoft::WRL::ComPtr;

namespace rs::render {

namespace {

// ルート定数(HLSL の LuminanceMeterConstants と並びを一致させる。float2 は 16 バイトの境界をまたがない)。
struct LuminanceMeterShaderConstants {
    float inputSize[2];
    float gridSize[2];
    std::uint32_t colorIdx;
    float minLuminance;
    float maxLuminance;
    std::uint32_t pad;
};
constexpr std::uint32_t kConstantDwordCount = sizeof(LuminanceMeterShaderConstants) / sizeof(std::uint32_t);
static_assert(sizeof(LuminanceMeterShaderConstants) == 32, "LuminanceMeterConstants は 8 DWORD(HLSL の cbuffer と同じ並び)");

// テクスチャ→バッファのコピーの行ピッチ(32 画素 x 4 バイト = 128 バイトを、コピーの行配置の単位へ切り上げる)。
constexpr UINT kReadbackRowPitch = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
constexpr UINT64 kReadbackBytes = static_cast<UINT64>(kReadbackRowPitch) * LuminanceMeterGrid::kHeight;

} // namespace

struct LuminanceMeterPass::Impl {
    ComPtr<ID3D12Device> device;
    FullscreenPassPipelines pipelines;
    SceneHdrTarget output; // R32_FLOAT の格子
    ComPtr<ID3D12Resource> readback;
    int pollsLeft = 0; // 0=読み戻し待ちなし
};

LuminanceMeterPass::LuminanceMeterPass() = default;
LuminanceMeterPass::~LuminanceMeterPass() = default;

bool LuminanceMeterPass::Init(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap,
                              std::uint32_t firstSrvIndex) {
    impl_.reset();
    if (!device.Valid() || !srvHeap.Valid()) return false;
    std::unique_ptr<Impl> impl = std::make_unique<Impl>();
    impl->device = device.Native();
    if (!impl->pipelines.Init(device, srvHeap, kLuminanceMeterShader, "luminance_meter", kConstantDwordCount) ||
        !impl->pipelines.AddPixelShader(L"PSMeter", HdrTargetFormat::R32Float))
        return false;
    const float black[3] = {0.0f, 0.0f, 0.0f};
    if (!impl->output.Init(device, srvHeap, firstSrvIndex, black, HdrTargetFormat::R32Float)) return false;
    impl_ = std::move(impl);
    return true;
}

bool LuminanceMeterPass::CreateResources() {
    if (!impl_) return false;
    Impl& impl = *impl_;
    if (impl.output.Ready() && impl.readback) return true;
    if (!impl.output.Resize(LuminanceMeterGrid::kWidth, LuminanceMeterGrid::kHeight)) {
        spdlog::error("[luminance_meter] 出力を作れませんでした");
        return false;
    }
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = kReadbackBytes;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(impl.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
                                                    nullptr, IID_PPV_ARGS(&impl.readback)))) {
        spdlog::error("[luminance_meter] 読み戻しバッファを作れませんでした");
        return false;
    }
    impl.pollsLeft = 0;
    return true;
}

bool LuminanceMeterPass::Ready() const { return impl_ && impl_->output.Ready() && impl_->readback; }

void LuminanceMeterPass::Record(const rs::rhi::CommandList& commandList, SceneHdrTarget& input) {
    if (!Ready() || !commandList.Valid() || !input.Ready() || impl_->pollsLeft > 0) return;
    Impl& impl = *impl_;

    LuminanceMeterShaderConstants constants = {};
    constants.inputSize[0] = static_cast<float>(input.Width());
    constants.inputSize[1] = static_cast<float>(input.Height());
    constants.gridSize[0] = static_cast<float>(LuminanceMeterGrid::kWidth);
    constants.gridSize[1] = static_cast<float>(LuminanceMeterGrid::kHeight);
    constants.colorIdx = input.SrvIndex();
    constants.minLuminance = kMinLuminance;
    constants.maxLuminance = kMaxLuminance;

    input.Transition(commandList, HdrTargetState::ShaderResource);
    impl.pipelines.Bind(commandList, &constants);
    impl.pipelines.Draw(commandList, impl.output, 0);
    // 描いた出力の大きさのビューポートが残るので、後段のために入力の大きさへ戻す。
    commandList.SetViewportAndScissor(0.0f, 0.0f, static_cast<float>(input.Width()), static_cast<float>(input.Height()));
    if (impl.output.RecordCopyToBuffer(commandList, impl.readback.Get(), kReadbackRowPitch)) {
        impl.pollsLeft = kReadbackDelayPolls;
    }
}

bool LuminanceMeterPass::Poll(LuminanceMeterGrid& grid) {
    if (!impl_ || impl_->pollsLeft <= 0 || !impl_->readback) return false;
    if (--impl_->pollsLeft > 0) return false;
    std::uint8_t* data = nullptr;
    const D3D12_RANGE range = {0, static_cast<SIZE_T>(kReadbackBytes)};
    if (FAILED(impl_->readback->Map(0, &range, reinterpret_cast<void**>(&data)))) {
        spdlog::error("[luminance_meter] 読み戻しバッファの Map に失敗しました");
        return false;
    }
    for (std::uint32_t y = 0; y < LuminanceMeterGrid::kHeight; ++y) {
        std::memcpy(&grid.logLuminance[static_cast<std::size_t>(y) * LuminanceMeterGrid::kWidth],
                    data + static_cast<std::size_t>(y) * kReadbackRowPitch, LuminanceMeterGrid::kWidth * sizeof(float));
    }
    const D3D12_RANGE written = {0, 0};
    impl_->readback->Unmap(0, &written);
    return true;
}

} // namespace rs::render

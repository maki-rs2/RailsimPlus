//
// TonemapCheck.cpp - TonemapCheck.h の実装
//
#include "TonemapCheck.h"

#include <d3d12.h>
#include <wrl/client.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>

#include <spdlog/spdlog.h>

#include "../../render/postprocess/PostProcessFormats.h"
#include "../../render/postprocess/SceneHdrTarget.h"
#include "../../render/postprocess/TonemapMath.h"
#include "../../render/postprocess/TonemapPass.h"
#include "HeadlessDevice.h"

namespace rs::app {
namespace {

using Microsoft::WRL::ComPtr;

// 検証に使うターゲットの大きさ(全画素が同じ値になる。左上の1画素だけ読む)。
constexpr std::uint32_t kTargetSize = 4;

// GPUのhalf float(クリア値)とsRGB 8ビットへの量子化で出る差の許容(8ビット値の差)。
constexpr int kToleranceBytes = 2;

constexpr DWORD kFenceWaitTimeoutMs = 10000;

// 入力の色(リニアRGB)。暗部・中間・明部の灰色と、色付きの明るさ(色全体で計算するモードの確認用)。
constexpr std::array<std::array<float, 3>, 12> kInputs = {{
    {0.0f, 0.0f, 0.0f},
    {0.001f, 0.001f, 0.001f},
    {0.02f, 0.02f, 0.02f},
    {0.18f, 0.18f, 0.18f},
    {0.5f, 0.5f, 0.5f},
    {1.0f, 1.0f, 1.0f},
    {2.0f, 2.0f, 2.0f},
    {8.0f, 8.0f, 8.0f},
    {64.0f, 64.0f, 64.0f},
    {0.9f, 0.3f, 0.05f},
    {0.05f, 0.2f, 1.5f},
    {4.0f, 0.5f, 0.1f},
}};
constexpr std::array<float, 2> kExposures = {1.0f, 0.5f};

// リニア(0..1)→sRGBのエンコード値(0..1)。出力先のRTVが行う変換と同じ。
float LinearToSrgb(float linear) {
    constexpr float kLinearThreshold = 0.0031308f;
    if (linear <= kLinearThreshold) return linear * 12.92f;
    return 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
}

// 1ケースずつ「HDR値を作る→トーンマップ→読み戻す」を回す道具立て。
class Harness {
public:
    bool Init(ID3D12Device* device) {
        device_ = device;
        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (FAILED(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue_)))) return false;
        if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator_))))
            return false;
        if (FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator_.Get(), nullptr,
                                             IID_PPV_ARGS(&list_))))
            return false;
        list_->Close();
        if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)))) return false;
        fenceEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (fenceEvent_ == nullptr) return false;

        // HDRターゲットのSRVを置く1枠ヒープ。
        D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
        srvHeapDesc.NumDescriptors = 1;
        srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&srvHeap_)))) return false;
        srvStride_ = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        // 出力先(トーンマップの出力形式。sRGB変換はRTVが行う)と読み戻しバッファ。
        D3D12_HEAP_PROPERTIES defaultHeap = {};
        defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC rtDesc = {};
        rtDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rtDesc.Width = kTargetSize;
        rtDesc.Height = kTargetSize;
        rtDesc.DepthOrArraySize = 1;
        rtDesc.MipLevels = 1;
        rtDesc.Format = rs::render::kTonemapOutputFormat;
        rtDesc.SampleDesc.Count = 1;
        rtDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        if (FAILED(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &rtDesc,
                                                   D3D12_RESOURCE_STATE_COPY_SOURCE, nullptr,
                                                   IID_PPV_ARGS(&renderTarget_))))
            return false;
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
        rtvHeapDesc.NumDescriptors = 1;
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        if (FAILED(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvHeap_)))) return false;
        device->CreateRenderTargetView(renderTarget_.Get(), nullptr, rtvHeap_->GetCPUDescriptorHandleForHeapStart());

        UINT64 totalBytes = 0;
        device->GetCopyableFootprints(&rtDesc, 0, 1, 0, &footprint_, nullptr, nullptr, &totalBytes);
        D3D12_HEAP_PROPERTIES readbackHeap = {};
        readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC bufferDesc = {};
        bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDesc.Width = totalBytes;
        bufferDesc.Height = 1;
        bufferDesc.DepthOrArraySize = 1;
        bufferDesc.MipLevels = 1;
        bufferDesc.SampleDesc.Count = 1;
        bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        return SUCCEEDED(device->CreateCommittedResource(&readbackHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                                                         D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                         IID_PPV_ARGS(&readback_)));
    }

    ~Harness() {
        WaitIdle();
        if (fenceEvent_ != nullptr) CloseHandle(fenceEvent_);
    }

    // リニアRGB color のHDR画素を params でトーンマップし、出力の左上の画素(sRGB 8ビットのRGB)を out へ返す。
    bool Run(const rs::render::TonemapPass& tonemap, const rs::render::TonemapParams& params,
             const std::array<float, 3>& color, std::array<int, 3>& out) {
        // クリア色が入力値になる(BeginSceneがクリアする)。作り直すたびに最適化クリア値も合う。
        rs::render::SceneHdrTarget hdr;
        if (!hdr.Init(rs::rhi::Device(device_), rs::rhi::SrvHeap(srvHeap_.Get(), srvStride_), 0, color.data()) ||
            !hdr.Resize(kTargetSize, kTargetSize))
            return false;
        if (FAILED(allocator_->Reset()) || FAILED(list_->Reset(allocator_.Get(), nullptr))) return false;
        const rs::rhi::CommandList commandList(list_.Get());

        Barrier(renderTarget_.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        hdr.BeginScene(commandList, rs::rhi::CpuDescriptor{});
        hdr.Transition(commandList, rs::render::HdrTargetState::ShaderResource);
        const D3D12_VIEWPORT viewport = {0.0f, 0.0f, float(kTargetSize), float(kTargetSize), 0.0f, 1.0f};
        const D3D12_RECT scissor = {0, 0, LONG(kTargetSize), LONG(kTargetSize)};
        list_->RSSetViewports(1, &viewport);
        list_->RSSetScissorRects(1, &scissor);
        tonemap.Record(commandList, hdr, rs::rhi::CpuDescriptor{rtvHeap_->GetCPUDescriptorHandleForHeapStart().ptr},
                       params);
        Barrier(renderTarget_.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

        D3D12_TEXTURE_COPY_LOCATION dst = {};
        dst.pResource = readback_.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint = footprint_;
        D3D12_TEXTURE_COPY_LOCATION src = {};
        src.pResource = renderTarget_.Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.SubresourceIndex = 0;
        list_->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        if (FAILED(list_->Close())) return false;
        ID3D12CommandList* lists[] = {list_.Get()};
        queue_->ExecuteCommandLists(1, lists);
        if (!WaitIdle()) return false;

        const std::uint8_t* mapped = nullptr;
        if (FAILED(readback_->Map(0, nullptr, reinterpret_cast<void**>(const_cast<std::uint8_t**>(&mapped))))) {
            spdlog::error("[tonemap_check] 読み戻しバッファのMapに失敗しました");
            return false;
        }
        for (int c = 0; c < 3; ++c) out[c] = mapped[footprint_.Offset + c];
        const D3D12_RANGE noWrite = {0, 0};
        readback_->Unmap(0, &noWrite);
        return true;
    }

private:
    void Barrier(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = resource;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = before;
        barrier.Transition.StateAfter = after;
        list_->ResourceBarrier(1, &barrier);
    }

    bool WaitIdle() {
        if (!queue_ || !fence_ || fenceEvent_ == nullptr) return true;
        ++fenceValue_;
        if (FAILED(queue_->Signal(fence_.Get(), fenceValue_))) return false;
        if (fence_->GetCompletedValue() >= fenceValue_) return true;
        if (FAILED(fence_->SetEventOnCompletion(fenceValue_, fenceEvent_))) return false;
        return WaitForSingleObject(fenceEvent_, kFenceWaitTimeoutMs) == WAIT_OBJECT_0;
    }

    ID3D12Device* device_ = nullptr;
    ComPtr<ID3D12CommandQueue> queue_;
    ComPtr<ID3D12CommandAllocator> allocator_;
    ComPtr<ID3D12GraphicsCommandList> list_;
    ComPtr<ID3D12Fence> fence_;
    HANDLE fenceEvent_ = nullptr;
    UINT64 fenceValue_ = 0;
    ComPtr<ID3D12DescriptorHeap> srvHeap_;
    UINT srvStride_ = 0;
    ComPtr<ID3D12Resource> renderTarget_;
    ComPtr<ID3D12DescriptorHeap> rtvHeap_;
    ComPtr<ID3D12Resource> readback_;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint_ = {};
};

} // namespace

int RunTonemapCheck() {
    ComPtr<ID3D12Device> device = CreateHeadlessDevice();
    if (!device) {
        spdlog::error("[tonemap-check] D3D12デバイスを作れませんでした");
        return 1;
    }
    rs::render::TonemapPass tonemap;
    if (!tonemap.Init(rs::rhi::Device(device.Get()), rs::render::TonemapParams{})) {
        spdlog::error("[tonemap-check] トーンマップのPSOを作れませんでした");
        return 1;
    }
    int checked = 0;
    int mismatched = 0;
    int failedRuns = 0;
    {
        Harness harness;
        if (!harness.Init(device.Get())) {
            spdlog::error("[tonemap-check] 検証用のGPU資源を作れませんでした");
            return 1;
        }
        for (std::size_t modeIndex = 0; modeIndex < rs::render::kTonemapModeCount; ++modeIndex) {
            for (const float exposure : kExposures) {
                rs::render::TonemapParams params;
                params.mode = static_cast<rs::render::TonemapMode>(modeIndex);
                params.exposure = exposure;
                for (const std::array<float, 3>& input : kInputs) {
                    std::array<int, 3> gpu = {};
                    if (!harness.Run(tonemap, params, input, gpu)) {
                        spdlog::error("[tonemap-check] 実行に失敗しました mode={} exposure={}", modeIndex, exposure);
                        ++failedRuns;
                        continue;
                    }
                    float cpuLinear[3] = {};
                    rs::render::TonemapColor(params, input.data(), cpuLinear);
                    ++checked;
                    for (int c = 0; c < 3; ++c) {
                        const int expected = static_cast<int>(std::lround(LinearToSrgb(cpuLinear[c]) * 255.0f));
                        if (std::abs(gpu[c] - expected) <= kToleranceBytes) continue;
                        spdlog::error("[tonemap-check] ずれ mode={} exposure={} 入力=({}, {}, {}) ch={} GPU={} CPU={}",
                                      modeIndex, exposure, input[0], input[1], input[2], c, gpu[c], expected);
                        ++mismatched;
                        break;
                    }
                }
            }
        }
    }
    const int debugMessages = ReportDebugMessages(device.Get(), "tonemap-check");
    spdlog::info("[tonemap-check] {}ケース中、ずれ={} 実行失敗={} D3D12の警告/エラー={}件", checked + failedRuns,
                 mismatched, failedRuns, debugMessages);
    return (mismatched == 0 && failedRuns == 0 && debugMessages == 0) ? 0 : 1;
}

} // namespace rs::app

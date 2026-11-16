//
// ThumbnailResources.cpp - ThumbnailRenderer::Impl のGPU資源の作成・フェンス待ち・リードバック
//
#include <cstring>

#include <spdlog/spdlog.h>

#include "../postprocess/PostProcessFormats.h"
#include "ThumbnailRendererImpl.h"

namespace rs::render {

namespace {

// 背景の上下2色(青寄りのグレー)。sRGBとしての見た目で選んだ値で、sRGB空間で補間してからリニアへ変換し、HDRターゲットへ詰める。
constexpr float kThumbnailBackgroundTop[3] = {0.434f, 0.476f, 0.532f};    // = {0.62, 0.68, 0.76} × 0.7
constexpr float kThumbnailBackgroundBottom[3] = {0.252f, 0.280f, 0.329f}; // = {0.36, 0.40, 0.47} × 0.7

// 最終出力RTの形式(トーンマップの出力形式 kTonemapOutputFormat と同じ。リードバック用)。
constexpr DXGI_FORMAT kThumbnailRtFormat = kTonemapOutputFormat;

// フェンス待ちの上限。1枚の撮影は数十ミリ秒で終わるはずなので、これを超えたらGPUの異常とみなす。
constexpr DWORD kFenceWaitTimeoutMs = 10000;

} // namespace

bool ThumbnailRenderer::Impl::CreateQueueAndSync() {
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)))) return false;
    if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))))
        return false;
    if (FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
                                         IID_PPV_ARGS(&cmdList))))
        return false;
    cmdList->Close(); // 撮影のたびに Reset してから使う
    if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) return false;
    fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    return fenceEvent != nullptr;
}

bool ThumbnailRenderer::Impl::CreateTargets() {
    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rtDesc = {};
    rtDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rtDesc.Width = config.width;
    rtDesc.Height = config.height;
    rtDesc.DepthOrArraySize = 1;
    rtDesc.MipLevels = 1;
    rtDesc.Format = kThumbnailRtFormat;
    rtDesc.SampleDesc.Count = 1;
    rtDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    // 最適化クリア値は付けない(RTはトーンマップが全面を上書きするので、クリアは使わない)。
    if (FAILED(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &rtDesc,
                                               D3D12_RESOURCE_STATE_COPY_SOURCE, nullptr,
                                               IID_PPV_ARGS(&renderTarget))))
        return false;

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = 1;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    if (FAILED(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvHeap)))) return false;
    device->CreateRenderTargetView(renderTarget.Get(), nullptr,
                                   rtvHeap->GetCPUDescriptorHandleForHeapStart());

    // 行ピッチ(D3D12_TEXTURE_DATA_PITCH_ALIGNMENTの倍数)は GetCopyableFootprints に決めさせる。
    // リードバックは最終出力RTの配置(footprint)の1枚ぶんのバッファ。
    UINT64 totalBytes = 0;
    device->GetCopyableFootprints(&rtDesc, 0, 1, 0, &footprint, nullptr, nullptr, &totalBytes);
    if (!CreateBuffer(D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST, totalBytes, readback))
        return false;
    return CreateHdrTarget();
}

bool ThumbnailRenderer::Impl::CreateHdrTarget() {
    // トーンマップの入力SRVを置く、このレンダラ専用の1枠ヒープ(本編のSRVヒープとは別)。
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = 1;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&hdrSrvHeap)))) return false;
    const UINT srvSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    // クリアは使わない(背景のコピーで全面を埋める)ので、クリア色は0でよい。
    const float clearColor[3] = {0.0f, 0.0f, 0.0f};
    if (!hdrTarget.Init(rs::rhi::Device(device.Get()), rs::rhi::SrvHeap(hdrSrvHeap.Get(), srvSize), 0,
                        clearColor) ||
        !hdrTarget.Resize(config.width, config.height))
        return false;
    // 背景の上下のグラデーション(撮影のたびにHDRターゲットへコピーする)。
    return hdrTarget.SetBackgroundGradient(kThumbnailBackgroundTop, kThumbnailBackgroundBottom);
}

bool ThumbnailRenderer::Impl::CreateBuffer(D3D12_HEAP_TYPE heapType, D3D12_RESOURCE_STATES state, UINT64 size,
                                           ComPtr<ID3D12Resource>& out) {
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = heapType;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return SUCCEEDED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
                                                     IID_PPV_ARGS(&out)));
}

bool ThumbnailRenderer::Impl::CreateSrvHeapAndPipeline(const MeshPipelineSharedState& shared) {
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = config.srvStride + ThumbnailEnvironment::SrvCount() + WboitPass::kSrvCount;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&srvHeap)))) return false;

    // 本編の FrameRenderer::EnsureSceneSlotCapacity と同じ並びで、レンジの先頭(0)から配る。
    const UINT srvSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    const UINT firstTex = 0;
    const UINT firstTransBuf = firstTex + config.maxTextures;
    const UINT firstPartWorld = firstTransBuf + config.transBufSrvCount;
    const UINT firstOverride = firstPartWorld + config.partWorldSrvCount;
    if (!pipeline.Init(device.Get(), shared, srvHeap.Get(), srvSize, firstTex, config.maxTextures,
                       firstTransBuf, firstPartWorld, firstOverride, config.maxParts, textureManager))
        return false;
    if (!pipeline.CreateDepthBuffer(device.Get(), config.width, config.height)) return false;

    // 半透明の合成。作れなくても撮影は続けられる(ソート版で描く)。
    const rs::rhi::Device rhiDevice(device.Get());
    if (!viewConstants.Init(rhiDevice, 1)) return false;
    const std::uint32_t wboitSrvIndex = config.srvStride + ThumbnailEnvironment::SrvCount();
    if (!wboit.Init(rhiDevice, rs::rhi::SrvHeap(srvHeap.Get(), srvSize), wboitSrvIndex) ||
        !wboit.CreateTargets(rhiDevice, config.width, config.height)) {
        spdlog::warn("[thumbnail] WBOITを初期化できませんでした。半透明はソート版の合成で撮ります");
    } else {
        spdlog::info("[thumbnail] {}x{}: 半透明はWBOITで合成します", config.width, config.height);
    }
    return true;
}

void ThumbnailRenderer::Impl::GenerateEnvironment() {
    const UINT srvSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    bool generated = false;
    if (environment.Init(rs::rhi::Device(device.Get()), rs::rhi::SrvHeap(srvHeap.Get(), srvSize),
                         config.srvStride) &&
        SUCCEEDED(allocator->Reset()) && SUCCEEDED(cmdList->Reset(allocator.Get(), nullptr))) {
        // 大気LUT・IBLの Update が自分でSRVヒープを設定する。
        environment.RecordGeneration(rs::rhi::CommandList(cmdList.Get()));
        if (SUCCEEDED(cmdList->Close())) {
            ID3D12CommandList* lists[] = {cmdList.Get()};
            queue->ExecuteCommandLists(1, lists);
            generated = WaitIdle() && environment.Ready();
        }
    }
    if (!generated) spdlog::warn("[thumbnail] IBLの生成に失敗しました。環境光なしで撮ります");
}

bool ThumbnailRenderer::Impl::WaitIdle() {
    if (!queue || !fence || !fenceEvent) return true;
    ++fenceValue;
    if (FAILED(queue->Signal(fence.Get(), fenceValue))) return false;
    if (fence->GetCompletedValue() >= fenceValue) return true;
    if (FAILED(fence->SetEventOnCompletion(fenceValue, fenceEvent))) return false;
    return WaitForSingleObject(fenceEvent, kFenceWaitTimeoutMs) == WAIT_OBJECT_0;
}

void ThumbnailRenderer::Impl::CheckDeviceLost() {
    const HRESULT reason = device->GetDeviceRemovedReason();
    if (FAILED(reason)) {
        spdlog::error("[thumbnail] デバイスが削除されました (hr=0x{:08X})。以後のサムネイルは撮りません",
                      static_cast<unsigned int>(reason));
        usable = false;
    }
}

bool ThumbnailRenderer::Impl::ReadBack(ImageData& out) {
    D3D12_RANGE readRange = {0, static_cast<SIZE_T>(footprint.Footprint.RowPitch) * config.height};
    uint8_t* mapped = nullptr;
    if (FAILED(readback->Map(0, &readRange, reinterpret_cast<void**>(&mapped)))) {
        spdlog::error("[thumbnail] 読み戻しバッファのMapに失敗しました");
        return false;
    }
    out.width = config.width;
    out.height = config.height;
    out.pixels.resize(static_cast<size_t>(config.width) * config.height * 4);
    for (std::uint32_t y = 0; y < config.height; ++y) {
        const uint8_t* srcRow = mapped + static_cast<size_t>(y) * footprint.Footprint.RowPitch;
        uint8_t* dstRow = out.pixels.data() + static_cast<size_t>(y) * config.width * 4;
        std::memcpy(dstRow, srcRow, static_cast<size_t>(config.width) * 4);
        // 背景は不透明なので、結果も不透明にそろえる(半透明のブレンドで残ったアルファが
        // カードの下地を透かして見せないように)。
        for (std::uint32_t x = 0; x < config.width; ++x) dstRow[x * 4 + 3] = 255;
    }
    const D3D12_RANGE noWrite = {0, 0};
    readback->Unmap(0, &noWrite);
    out.ok = true;
    return true;
}

} // namespace rs::render

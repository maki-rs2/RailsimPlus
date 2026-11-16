#include "GpuContext.h"
#include "DeviceLostResult.h"

#include <chrono>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <string>

#include "../../platform/text/Encoding.h"
#include "../../render/pipeline/MeshPipelineTypes.h"

namespace rs::app {

bool GpuContext::Init(HWND hwnd, UINT width, UINT height, UINT srvHeapDescriptorCount) {
    deviceLost_ = false;
    deviceLostReason_ = S_OK;
    width_ = width;
    height_ = height;
    if (!CreateDeviceAndSwapChain(hwnd, width, height, srvHeapDescriptorCount)) return false;
    if (!CreateRenderTargets()) return false;
    // 失敗してもfalseは返さない(GPUパス別計測は診断機能であり、これが使えない
    // 環境でも本体の描画は継続できる。失敗時はGpuFrameTimer側で警告ログ済み)。
    gpuTimer_.Init(device_.Get(), queue_.Get(), kFrameCount);
    return true;
}

bool GpuContext::FailInit(const std::string& what, HRESULT hr) {
    spdlog::error("{} failed (HRESULT 0x{:08X})", what, static_cast<unsigned>(hr));
    initFailure_.kind = GpuInitFailure::Kind::Other;
    initFailure_.hr = hr;
    return false;
}

bool GpuContext::CreateDeviceAndSwapChain(HWND hwnd, UINT width, UINT height,
                                          UINT srvHeapDescriptorCount) {
    UINT factoryFlags = 0;
#if defined(_DEBUG)
    {
        ComPtr<ID3D12Debug> dbg;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg)))) {
            dbg->EnableDebugLayer();
            factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
            spdlog::info("D3D12 debug layer enabled");
        }
    }
#endif
    // DRED(Device Removed Extended Data): DXGI device lost発生時にGPUの
    // 実行履歴(オートブレッドクラム)とページフォルト対象アロケーションを
    // 取得できるようにする。Debugビルド限定にしない(Releaseビルドでデバイスロストが
    // 起きたときに、Visual Studioグラフィックスデバッガを使わずログだけで
    // 一次切り分けできるようにするため)。
    // デバイス作成前に有効化しないと効果が無い。
    using InitClock = std::chrono::steady_clock;
    const InitClock::time_point tDred0 = InitClock::now();
    {
        ComPtr<ID3D12DeviceRemovedExtendedDataSettings1> dredSettings;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dredSettings)))) {
            dredSettings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
            dredSettings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
            spdlog::info("DRED (auto-breadcrumbs + page fault reporting) enabled");
        }
    }
    const InitClock::time_point tFactory0 = InitClock::now();
    if (const HRESULT hr = CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&factory_)); FAILED(hr))
        return FailInit("CreateDXGIFactory2", hr);
    const InitClock::time_point tAdapter0 = InitClock::now();
    // [diag] デバッグレイヤーの検証メッセージ(通常はOutputDebugString経由でVSの
    // 出力ウィンドウにしか出ず、コンソール/spdlogログには現れない)をこちらへも
    // 転送する。TDR/device removed の原因(不正なAPI使用等)を追跡するため
    // (device_ 作成後に別途 QueryInterface する。device_ が無ければ何もしない)。

    if (!CreateDevice()) return false;
    {
        const InitClock::time_point now = InitClock::now();
        auto ms = [](InitClock::time_point a, InitClock::time_point b) {
            return std::chrono::duration<double, std::milli>(b - a).count();
        };
        spdlog::info("[startup] d3d12内訳: dred={:.0f}ms factory={:.0f}ms adapter+createDevice={:.0f}ms",
                     ms(tDred0, tFactory0), ms(tFactory0, tAdapter0), ms(tAdapter0, now));
    }
    if (SUCCEEDED(device_.As(&infoQueue_))) {
        spdlog::info("D3D12 info queue attached (validation messages -> spdlog)");
    }
    D3D12_COMMAND_QUEUE_DESC qd = {};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (const HRESULT hr = device_->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue_)); FAILED(hr))
        return FailInit("CreateCommandQueue", hr);

    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.BufferCount = kFrameCount;
    sd.Width = width;
    sd.Height = height;
    sd.Format = kBackBufferFormat;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.SampleDesc.Count = 1;
    // VSyncオフでティアリング付きのPresentを使えるよう、対応環境ではスワップチェーンの作成時に許可しておく
    // (後から切り替えられない)。非対応環境ではフラグなしで、VSyncオフでもPresent(0, 0)になる。
    BOOL tearingSupported = FALSE;
    if (SUCCEEDED(factory_->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearingSupported,
                                                sizeof(tearingSupported))) &&
        tearingSupported) {
        swapChainFlags_ = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    }
    sd.Flags = swapChainFlags_;

    ComPtr<IDXGISwapChain1> sc1;
    if (const HRESULT hr = factory_->CreateSwapChainForHwnd(queue_.Get(), hwnd, &sd, nullptr,
                                                            nullptr, &sc1);
        FAILED(hr)) return FailInit("CreateSwapChainForHwnd", hr);
    factory_->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
    sc1.As(&swapChain_);
    frameIndex_ = swapChain_->GetCurrentBackBufferIndex();

    D3D12_DESCRIPTOR_HEAP_DESC rd = {};
    rd.NumDescriptors = kFrameCount * 2;
    rd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    if (const HRESULT hr = device_->CreateDescriptorHeap(&rd, IID_PPV_ARGS(&rtvHeap_)); FAILED(hr))
        return FailInit("CreateDescriptorHeap(RTV)", hr);
    rtvStride_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_DESCRIPTOR_HEAP_DESC srvd = {};
    srvd.NumDescriptors = srvHeapDescriptorCount;
    srvd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (const HRESULT hr = device_->CreateDescriptorHeap(&srvd, IID_PPV_ARGS(&srvHeap_)); FAILED(hr)) {
        return FailInit(fmt::format("CreateDescriptorHeap(SRV, {}個)", srvHeapDescriptorCount), hr);
    }
    srvStride_ =
        device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    for (UINT i = 0; i < kFrameCount; ++i) {
        if (const HRESULT hr = device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                               IID_PPV_ARGS(&cmdAlloc_[i]));
            FAILED(hr)) return FailInit("CreateCommandAllocator", hr);
    }
    if (const HRESULT hr = device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                      cmdAlloc_[frameIndex_].Get(), nullptr,
                                                      IID_PPV_ARGS(&cmdList_));
        FAILED(hr)) return FailInit("CreateCommandList", hr);
    cmdList_->Close();

    if (const HRESULT hr = device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
        FAILED(hr)) return FailInit("CreateFence", hr);
    fenceEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!fenceEvent_) return FailInit("CreateEvent(フェンス用)", HRESULT_FROM_WIN32(GetLastError()));
    return true;
}

bool GpuContext::CreateRenderTargets() {
    D3D12_CPU_DESCRIPTOR_HANDLE h = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < kFrameCount; ++i) {
        if (const HRESULT hr = swapChain_->GetBuffer(i, IID_PPV_ARGS(&backBuffers_[i])); FAILED(hr))
            return FailInit("IDXGISwapChain::GetBuffer", hr);
        D3D12_RENDER_TARGET_VIEW_DESC sceneDesc = {};
        sceneDesc.Format = kBackBufferRtvFormat;
        sceneDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        device_->CreateRenderTargetView(backBuffers_[i].Get(), &sceneDesc, h);
        h.ptr += rtvStride_;
    }
    for (UINT i = 0; i < kFrameCount; ++i) {
        D3D12_RENDER_TARGET_VIEW_DESC uiDesc = {};
        uiDesc.Format = kBackBufferFormat;
        uiDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        device_->CreateRenderTargetView(backBuffers_[i].Get(), &uiDesc, h);
        h.ptr += rtvStride_;
    }
    return true;
}

ID3D12GraphicsCommandList* GpuContext::ResetCmdList() {
    cmdAlloc_[frameIndex_]->Reset();
    cmdList_->Reset(cmdAlloc_[frameIndex_].Get(), nullptr);
    return cmdList_.Get();
}

void GpuContext::CloseAndExecuteCmdList() {
    cmdList_->Close();
    ID3D12CommandList* lists[] = {cmdList_.Get()};
    queue_->ExecuteCommandLists(1, lists);
}

GpuContext::FrameTargets GpuContext::BeginFrame(D3D12_CPU_DESCRIPTOR_HANDLE dsv) {
    // frameIndex_はMoveToNextFrame()でこのフレームのスロットへ更新済みで、
    // そのスロットのGPU処理完了はフェンス待機済み(前回EndFrame()の末尾)。
    // よってこの時点でそのスロットへ前回書かれたタイムスタンプ結果を問題なく
    // 読み出せる(GpuFrameTimer::BeginFrameの約束事)。
    gpuTimer_.BeginFrame(frameIndex_);

    ID3D12GraphicsCommandList* cmdList = ResetCmdList();
    gpuTimer_.Begin(cmdList, kGpuRegionFrame);

    D3D12_RESOURCE_BARRIER toRT = {};
    toRT.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toRT.Transition.pResource = backBuffers_[frameIndex_].Get();
    toRT.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    toRT.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    toRT.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(1, &toRT);

    D3D12_CPU_DESCRIPTOR_HANDLE sceneRtv = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    sceneRtv.ptr += SIZE_T(frameIndex_) * rtvStride_;
    D3D12_CPU_DESCRIPTOR_HANDLE uiRtv = sceneRtv;
    uiRtv.ptr += SIZE_T(kFrameCount) * rtvStride_;
    const float clear[] = {kBackgroundClearSrgb[0], kBackgroundClearSrgb[1], kBackgroundClearSrgb[2], 1.0f};
    cmdList->OMSetRenderTargets(1, &uiRtv, FALSE, dsv.ptr ? &dsv : nullptr);
    cmdList->ClearRenderTargetView(uiRtv, clear, 0, nullptr);
    cmdList->OMSetRenderTargets(1, &sceneRtv, FALSE, dsv.ptr ? &dsv : nullptr);
    if (dsv.ptr)
        // Reversed-Z: 遠方(0)でクリアし、GREATERで深度テストする
        cmdList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, rs::render::kReversedZClearDepth, 0, 0, nullptr);

    D3D12_VIEWPORT vp = {0.0f, 0.0f, float(width_), float(height_), 0.0f, 1.0f};
    D3D12_RECT scRect = {0, 0, LONG(width_), LONG(height_)};
    cmdList->RSSetViewports(1, &vp);
    cmdList->RSSetScissorRects(1, &scRect);

    ID3D12DescriptorHeap* heaps[] = {srvHeap_.Get()};
    cmdList->SetDescriptorHeaps(1, heaps);

    return {cmdList, sceneRtv, uiRtv, frameIndex_, &gpuTimer_};
}

void GpuContext::EndFrame() {
    ID3D12GraphicsCommandList* cmdList = cmdList_.Get();

    D3D12_RESOURCE_BARRIER toRT = {};
    toRT.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toRT.Transition.pResource = backBuffers_[frameIndex_].Get();
    toRT.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    // UI込みの読み戻し: RENDER_TARGET状態のうちにREADBACKヒープへコピーする。
    // SceneOnlyの要求はApp::Renderが記録済み(未記録なら読み戻せず、取り出し側が失敗として扱う)。
    if (captureRequested_ && !captureRecorded_ && captureStage_ == BackBufferCaptureStage::AfterUi)
        RecordCaptureCopy(cmdList);

    D3D12_RESOURCE_BARRIER toPresent = toRT;
    toPresent.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    toPresent.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    cmdList->ResourceBarrier(1, &toPresent);

    gpuTimer_.End(cmdList, kGpuRegionFrame);
    gpuTimer_.Resolve(cmdList); // Close()前に呼ぶ必要がある

    cmdList->Close();

    ID3D12CommandList* lists[] = {cmdList};
    queue_->ExecuteCommandLists(1, lists);
    const std::chrono::steady_clock::time_point presentStart = std::chrono::steady_clock::now();
    // VSyncオフかつティアリング対応のときだけALLOW_TEARING付きでPresentする(それ以外は仕様上付けられない)。
    const UINT syncInterval = vsync_ ? 1 : 0;
    const UINT presentFlags =
        (!vsync_ && (swapChainFlags_ & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) != 0) ? DXGI_PRESENT_ALLOW_TEARING : 0;
    HRESULT presentHr = swapChain_->Present(syncInterval, presentFlags);
    lastPresentMs_ = std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - presentStart)
                          .count();
    if (IsDeviceLostResult(presentHr)) {
        MarkDeviceLost("Present", presentHr);
    } else if (device_ && device_->GetDeviceRemovedReason() != S_OK) {
        // Present が成功を返しても、デバイスが取り除かれていることがある。
        MarkDeviceLost("EndFrame", presentHr);
    }
    if (captureRequested_ && !deviceLost_) ReadBackCapture();

    DrainInfoQueue();
    MoveToNextFrame();
}

void GpuContext::MoveToNextFrame() {
    // キュー全体で単調増加する値を使う(フレームスロットごとに独立したカウンタ
    // を持たない)。同一フェンスに逆行する値をSignalしてしまうと、その値を
    // 待っているSetEventOnCompletionが二度と発火せずハングするため。
    const UINT64 v = nextFenceValue_++;
    queue_->Signal(fence_.Get(), v);
    fenceValues_[frameIndex_] = v;
    frameIndex_ = swapChain_->GetCurrentBackBufferIndex();
    if (fence_->GetCompletedValue() < fenceValues_[frameIndex_]) {
        const std::chrono::steady_clock::time_point waitStart = std::chrono::steady_clock::now();
        fence_->SetEventOnCompletion(fenceValues_[frameIndex_], fenceEvent_);
        WaitForSingleObject(fenceEvent_, INFINITE);
        lastFrameWaitMs_ = std::chrono::duration<double, std::milli>(
                                std::chrono::steady_clock::now() - waitStart)
                                .count();
    } else {
        lastFrameWaitMs_ = 0.0;
    }
    ReleaseFinishedUploads();
    retirements_.ReleaseCompleted(fence_->GetCompletedValue());
    retirements_.SetRetireFenceValue(nextFenceValue_);
}

void GpuContext::ReleaseFinishedUploads() {
    std::erase_if(pendingUploads_, [](const std::future<void>& upload) {
        return upload.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    });
}

void GpuContext::WaitForGpu() {
    // 全フレームスロット分のGPU処理完了を待つ完全フラッシュ。OnResize等、
    // スワップチェーンの全バックバッファ参照を解放する前に必ず全スロットの
    // 完了を確実にする必要がある箇所から呼ばれる。
    if (!queue_ || !fence_) return;
    const UINT64 v = nextFenceValue_++;
    queue_->Signal(fence_.Get(), v);
    if (fence_->GetCompletedValue() < v) {
        fence_->SetEventOnCompletion(v, fenceEvent_);
        WaitForSingleObject(fenceEvent_, INFINITE);
    }
    for (UINT i = 0; i < kFrameCount; ++i) fenceValues_[i] = v;
    // キューが空になったので、待っているスレッドもすぐ終わる。
    pendingUploads_.clear();
    retirements_.ReleaseCompleted(fence_->GetCompletedValue());
    retirements_.SetRetireFenceValue(nextFenceValue_);
}

bool GpuContext::OnResize(UINT width, UINT height) {
    if (deviceLost_ || !swapChain_ || width == 0 || height == 0) return false;
    if (width == width_ && height == height_) return false;
    WaitForGpu();
    for (UINT i = 0; i < kFrameCount; ++i) backBuffers_[i].Reset();
    swapChain_->ResizeBuffers(kFrameCount, width, height, kBackBufferFormat, swapChainFlags_);
    width_ = width;
    height_ = height;
    frameIndex_ = swapChain_->GetCurrentBackBufferIndex();
    CreateRenderTargets();
    return true;
}

void GpuContext::Shutdown() {
    WaitForGpu();
    if (fenceEvent_) {
        CloseHandle(fenceEvent_);
        fenceEvent_ = nullptr;
    }
}

} // namespace rs::app

//
// GpuContext.h - D3D12デバイス/スワップチェーン/ヒープ/フェンス同期
//
// 「DX12デバイス初期化とフレーム同期」を受け持つ。3パス描画(トラック/列車/シーン+
// GPUインスタンス+WBOIT)の記録は FrameRenderer が受け持ち、GpuContext は
// バックバッファのバリア/クリア/Present/フェンス待機/ヘッドレス読み戻しのみを
// 扱う。
//
#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <ResourceUploadBatch.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <future>
#include <vector>

#include "GpuInitFailure.h"
#include "GpuFrameTimer.h"
#include "../../render/rhi/CommandList.h"
#include "../../render/rhi/RetirementQueue.h"

namespace rs::app {

using Microsoft::WRL::ComPtr;

// スワップチェーンのバッファ数と背面バッファフォーマット。App.h/FrameRenderer
// 双方から参照される基礎定数のため、GpuContext.h を正典とする。
constexpr UINT kFrameCount = 2;
constexpr DXGI_FORMAT kBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr DXGI_FORMAT kBackBufferRtvFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

// 背景のクリア色(sRGBのエンコード値)。バックバッファへそのまま書く色で、HDRシーンターゲットの
// クリア値はこれをリニアへ変換したもの(トーンマップをパススルーにすると同じバイト値が出る)。
constexpr float kBackgroundClearSrgb[3] = {0.06f, 0.07f, 0.10f};

// プラグインビューアーモードの背景色。ライトON(昼)は sRGB #262D38(R38 G45 B56)、OFF(夜)は
// #0E1118(R14 G17 B24)。RailSim II のビューアーも背景をライトで切り替える(CInterfaceMode.cpp の
// DrawBackground)。画面上でこの色になるよう、HDRシーンターゲットのクリア値はトーンマップを逆算して
// 決める(App::SetViewerBackground)。
constexpr float kViewerBackgroundDaySrgb[3] = {38.0f / 255.0f, 45.0f / 255.0f, 56.0f / 255.0f};
constexpr float kViewerBackgroundNightSrgb[3] = {14.0f / 255.0f, 17.0f / 255.0f, 24.0f / 255.0f};

// バックバッファ読み戻しを撮る時点。
enum class BackBufferCaptureStage { AfterUi, SceneOnly };

enum class UploadCompletion { Wait, NoWait };

class GpuContext {
public:
    // デバイス/スワップチェーン/RTV・SRVヒープ/コマンドリスト/フェンスを作る。
    // srvHeapDescriptorCount は SRV ヒープの総ディスクリプタ数(App.h の
    // kSrvLayout.total)。
    bool Init(HWND hwnd, UINT width, UINT height, UINT srvHeapDescriptorCount);

    // スワップチェーンのバックバッファを作り直す。サイズ不変/0の場合は何もせず
    // false を返す(呼び出し側はこれをトリガーに深度バッファ等の再作成をスキップ
    // してよい)。
    bool OnResize(UINT width, UINT height);

    // デバイスロスト(Present の DEVICE_REMOVED/RESET などで検出)。立ったあとはこのデバイスでは描けず、
    // 作り直しもしない。保存と終了は呼び出し側(アプリ層)が受け持つ。理由とDREDは検出時にログへ出す。
    bool DeviceLost() const { return deviceLost_; }
    // 検出時の GetDeviceRemovedReason(未検出のときは S_OK)。
    HRESULT DeviceLostReason() const { return deviceLostReason_; }
    // Init が失敗したときの理由(成功時は kind が None)。起動を止めるダイアログが使う。
    const GpuInitFailure& InitFailure() const { return initFailure_; }
    // 開発用: デバイスを意図的に取り除く(ロストの経路を踏むため)。取り除けない環境では何もしない。
    void SimulateDeviceLost();

    void WaitForGpu();
    void Shutdown();
    rs::rhi::RetirementQueue& Retirements() { return retirements_; }

    // VSyncのオン/オフ(次のPresentから反映)。オフのとき、ティアリング対応のスワップチェーンなら
    // 画面の更新を待たずに表示する。
    void SetVSync(bool enabled) { vsync_ = enabled; }
    bool VSyncEnabled() const { return vsync_; }

    ID3D12Device* Device() const { return device_.Get(); }
    ID3D12CommandQueue* Queue() const { return queue_.Get(); }
    ID3D12DescriptorHeap* SrvHeap() const { return srvHeap_.Get(); }
    UINT SrvStride() const { return srvStride_; }
    UINT Width() const { return width_; }
    UINT Height() const { return height_; }

    // 現在のフレームスロット番号(0..kFrameCount-1)。RmlUi統合(RmlUiIntegration)が、
    // このスロットに紐づく自前の遅延削除・コンスタントバッファリング等をホストの
    // フレームスロットと同期させるために参照する。
    UINT FrameIndex() const { return frameIndex_; }

    // 直近のEndFrame()でのブロック時間計測(ms、性能調査用)。
    // Present自体の所要時間と、次フレームのバックバッファ確保待ち
    // (フェンス待機、待たずに済んだ場合は0)を分けて計測する。
    double LastPresentMs() const { return lastPresentMs_; }
    double LastFrameWaitMs() const { return lastFrameWaitMs_; }

    // パス別GPU実時間計測(性能調査用、GpuFrameTimer.h)。Init失敗時は常に0を返す。
    double LastGpuMs(size_t regionIndex) const { return gpuTimer_.LastMs(regionIndex); }

    // 3パス描画の記録先。App::LoadModel/UploadMeshToGpu等の単発アップロードと
    // App::Render の両方が、このフレームの cmdAlloc をReset()してから使う
    // 共有コマンドリスト。
    ID3D12GraphicsCommandList* ResetCmdList();
    ID3D12GraphicsCommandList* CmdList() const { return cmdList_.Get(); }

    // ResetCmdList()で開いたコマンドリストをClose()してキューへExecuteする
    // (待機はしない。呼び出し側が別途WaitForGpu()/フェンス待機を行う想定。
    // FrameRenderer::UploadMeshToGpu等、単発アップロード側が使う)。
    void CloseAndExecuteCmdList();

    struct FrameTargets {
        ID3D12GraphicsCommandList* cmdList;
        D3D12_CPU_DESCRIPTOR_HANDLE sceneRtv;
        D3D12_CPU_DESCRIPTOR_HANDLE uiRtv;
        UINT frameIndex;
        GpuFrameTimer* timer; // パス別GPU計測(性能調査用)。Init失敗時も非null(全メソッドが何もしない)
    };
    // 新しいフレームの記録を開始する: cmdAlloc/cmdListをReset()し、
    // PRESENT->RENDER_TARGETへバリアし、RTV/DSVを設定してクリアし、
    // ビューポート/シザー/SRVヒープを設定する。dsv.ptr==0ならDSVクリアを
    // スキップする(深度バッファ未作成時)。
    FrameTargets BeginFrame(D3D12_CPU_DESCRIPTOR_HANDLE dsv);

    // 3Dパス記録(+ImGui記録)が終わった後に呼ぶ: ヘッドレス読み戻し(要求時)→
    // RENDER_TARGET->PRESENTへバリア→Close/Execute→Present→(読み戻し要求時は
    // GPU完了待ち+Map)→デバッグレイヤーのメッセージをspdlogへ転送→次フレームへ。
    void EndFrame();

    // 次のEndFrame()が描いたバックバッファをRGBA8で読み戻す(ヘッドレス検証・F12スクリーンショット)。
    // AfterUi=UI込み(EndFrame内で記録)、SceneOnly=トーンマップ後・UI描画前
    // (App::RenderがRecordSceneOnlyCaptureIfRequestedを呼ぶ時点で記録)。
    // 要求時に前回の読み戻し結果は捨てる(古い画像を取り出さないため)。
    void RequestBackBufferCapture(BackBufferCaptureStage stage = BackBufferCaptureStage::AfterUi) {
        captureRequested_ = true;
        captureRecorded_ = false;
        captureStage_ = stage;
        captured_ = CapturedFrame{};
    }
    // SceneOnlyの要求があり未記録なら、いまの時点(バックバッファはRENDER_TARGET状態)でコピーを記録する。
    void RecordSceneOnlyCaptureIfRequested(rs::rhi::CommandList commandList);
    bool TakeCapturedBackBuffer(std::vector<uint8_t>& outPixels, UINT& outWidth,
                                UINT& outHeight, UINT& outRowPitch) const {
        if (!captured_.valid) return false;
        outPixels = captured_.pixels;
        outWidth = captured_.width;
        outHeight = captured_.height;
        outRowPitch = captured_.rowPitch;
        return true;
    }

    // テクスチャ事前ロード用の一時アップロードバッチを回す(App::RunUploadCommands
    // の実体。device_/queue_のみに依存するため、ここに置く)。
    // UploadCompletion::NoWaitは同じキューへ積むだけなので、この後に提出するフレームのコマンド
    // より先に実行される。fnが渡したデータはバッチが受付時にコピーする。描画ループの途中で呼ぶ
    // 経路だけが使い、古いリソースの寿命は呼び出し側で管理する。
    template <typename Fn>
    void RunUploadCommands(Fn&& fn, UploadCompletion completion = UploadCompletion::Wait) {
        try {
            DirectX::ResourceUploadBatch upload(device_.Get());
            upload.Begin();
            fn(device_.Get(), upload);
            std::future<void> finished = upload.End(queue_.Get());
            if (completion == UploadCompletion::Wait) {
                finished.get();
            } else {
                pendingUploads_.push_back(std::move(finished));
            }
        } catch (const std::exception& e) {
            LogUploadFailure(e.what());
            throw;
        }
    }

    template <typename Fn>
    void RunUploadCommandsChunked(size_t itemCount, size_t itemsPerBatch, Fn&& fn) {
        if (itemCount == 0) return;
        const size_t safeItemsPerBatch = std::max<size_t>(itemsPerBatch, 1);
        for (size_t begin = 0; begin < itemCount; begin += safeItemsPerBatch) {
            const size_t end = std::min(begin + safeItemsPerBatch, itemCount);
            RunUploadCommands([&](ID3D12Device* device, DirectX::ResourceUploadBatch& upload) {
                fn(device, upload, begin, end);
            });
        }
    }

private:
    bool deviceLost_ = false;
    GpuInitFailure initFailure_;
    // 失敗をログに出し、initFailure_ に Other として記録して false を返す。
    bool FailInit(const std::string& what, HRESULT hr);
    // アダプタを選んで device_ を作る(GpuDeviceCreate.cpp)。
    bool CreateDevice();
    HRESULT deviceLostReason_ = S_OK;
    // ロストを記録する(初回だけ、hr と GetDeviceRemovedReason と DRED をログへ出す)。
    void MarkDeviceLost(const char* where, HRESULT hr);
    rs::rhi::RetirementQueue retirements_;
    bool CreateDeviceAndSwapChain(HWND hwnd, UINT width, UINT height,
                                  UINT srvHeapDescriptorCount);
    bool CreateRenderTargets();
    void MoveToNextFrame();
    void ReleaseFinishedUploads();
    void DrainInfoQueue();
    void LogUploadFailure(const char* what);
    // DXGI device lost検出時にDRED(オートブレッドクラム+ページフォルト情報)を
    // spdlog::criticalへ出力する。
    void LogDredOutput();

    // std::asyncのfutureは破棄時に完了を待つので、終わるまでここで持つ。
    std::vector<std::future<void>>    pendingUploads_;
    ComPtr<IDXGIFactory6>             factory_;
    ComPtr<ID3D12Device>              device_;
    ComPtr<ID3D12InfoQueue>           infoQueue_; // デバッグレイヤーのメッセージをspdlogへ転送する用(Debugビルドのみ)
    ComPtr<ID3D12CommandQueue>        queue_;
    ComPtr<IDXGISwapChain3>           swapChain_;
    ComPtr<ID3D12DescriptorHeap>      rtvHeap_;
    ComPtr<ID3D12DescriptorHeap>      srvHeap_;
    ComPtr<ID3D12Resource>            backBuffers_[kFrameCount];
    ComPtr<ID3D12CommandAllocator>    cmdAlloc_[kFrameCount];
    ComPtr<ID3D12GraphicsCommandList> cmdList_;
    ComPtr<ID3D12Fence>               fence_;
    HANDLE                            fenceEvent_ = nullptr;
    UINT64                            fenceValues_[kFrameCount] = {};
    UINT64                            nextFenceValue_ = 1; // キュー全体で単調増加する唯一のフェンス値カウンタ
    UINT                              frameIndex_ = 0;
    UINT                              rtvStride_ = 0;
    UINT                              srvStride_ = 0;
    UINT                              width_ = 0, height_ = 0;
    bool                              vsync_ = true;
    UINT                              swapChainFlags_ = 0; // ティアリング対応ならDXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING。ResizeBuffersにも同じ値を渡す

    // 性能調査用のブロック時間計測(ms)。EndFrame()で更新する。
    double                            lastPresentMs_ = 0.0;
    double                            lastFrameWaitMs_ = 0.0;
    GpuFrameTimer                     gpuTimer_; // パス別GPU実時間計測(性能調査用)

    // バックバッファのコピーをREADBACKバッファへ記録する(GpuContextCapture.cpp)。
    // バックバッファはRENDER_TARGET状態であること。
    void RecordCaptureCopy(ID3D12GraphicsCommandList* cmdList);
    // GPU完了待ち+Mapしてcaptured_へ格納し、要求を下ろす(Present後に呼ぶ)。
    void ReadBackCapture();

    // バックバッファ読み戻し(ヘッドレス検証・F12スクリーンショット)。
    bool captureRequested_ = false;
    bool captureRecorded_ = false; // 今回の要求でコピーを記録済みか
    BackBufferCaptureStage captureStage_ = BackBufferCaptureStage::AfterUi;
    ComPtr<ID3D12Resource> captureBuf_;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT captureLayout_ = {};
    struct CapturedFrame {
        bool                 valid = false;
        std::vector<uint8_t> pixels; // RGBA8、rowPitch詰め(パディング込み)
        UINT                 width = 0;
        UINT                 height = 0;
        UINT                 rowPitch = 0;
    } captured_;
};

} // namespace rs::app

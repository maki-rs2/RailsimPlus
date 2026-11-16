//
// ThumbnailRendererImpl.h - ThumbnailRenderer の実装の中身(DX12の資源)。ThumbnailRenderer*.cpp だけが読む内部ヘッダ
//
#pragma once

#include <d3d12.h>
#include <wrl/client.h>

#include "../pipeline/MeshPipeline.h"
#include "../pipeline/ViewConstantsBuffer.h"
#include "../postprocess/SceneHdrTarget.h"
#include "../resource/TextureManager.h"
#include "../wboit/WboitPass.h"
#include "ThumbnailEnvironment.h"
#include "ThumbnailRenderer.h"

namespace rs::render {

using Microsoft::WRL::ComPtr;

struct ThumbnailRenderer::Impl {
    ~Impl() {
        WaitIdle();
        if (fenceEvent) CloseHandle(fenceEvent);
    }

    bool CreateQueueAndSync();
    bool CreateTargets();
    bool CreateHdrTarget();
    bool CreateBuffer(D3D12_HEAP_TYPE heapType, D3D12_RESOURCE_STATES state, UINT64 size,
                      ComPtr<ID3D12Resource>& out);
    bool CreateSrvHeapAndPipeline(const MeshPipelineSharedState& shared);
    // 固定の環境光(大気LUT+IBL)を1回だけ作る。失敗しても撮影は続けられる(環境光なしで描く)。
    void GenerateEnvironment();

    // 直前までに積んだGPUの作業が終わるまで待つ。タイムアウト・失敗は false。
    bool WaitIdle();
    // 描き込んだあとの結果を読み出して out へ詰める。
    bool ReadBack(ImageData& out);
    void RecordScene(ID3D12GraphicsCommandList* cmdList, const ThumbnailScene& scene);
    // 背景→シーン描画→トーンマップ→リードバックへのコピーまでを記録する(scene が null なら背景だけ)。
    void RecordCommands(ID3D12GraphicsCommandList* list, const ThumbnailScene* scene);
    void CheckDeviceLost();

    ThumbnailRendererConfig config;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> cmdList;
    ComPtr<ID3D12Fence> fence;
    HANDLE fenceEvent = nullptr;
    UINT64 fenceValue = 0;

    ComPtr<ID3D12Resource> renderTarget;
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    ComPtr<ID3D12Resource> readback;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};    // 最終出力RT(リードバック)の配置

    // 撮影解像度のHDRターゲット。SRVはこのレンダラ専用の1枠ヒープに置く(トーンマップの入力用)。
    SceneHdrTarget hdrTarget;
    ComPtr<ID3D12DescriptorHeap> hdrSrvHeap;
    const TonemapPass* tonemap = nullptr;

    ComPtr<ID3D12DescriptorHeap> srvHeap;
    // 本編とは別の、このレンダラ専用の環境光。SRVは srvHeap のテクスチャ等のレンジ(0〜srvStride-1)の後ろに置く。
    // srvHeap より後に宣言する(GPU資源をヒープより先に破棄する)。
    ThumbnailEnvironment environment;
    // 半透明の合成(本編と同じ WBOIT)。使えない環境ではソート版の通常ブレンドで描く。
    // SRVは環境の枠の後ろ。srvHeap より後に宣言する(GPU資源をヒープより先に破棄する)。
    WboitPass wboit;
    // 撮影ごとに作って捨てるGPU資源の取得先。共有の TextureManager は完了前の資源をキャッシュへ
    // 入れるため、別キューのこのレンダラは自前のものを使う(MeshPipeline::Init のコメント参照)。
    TextureManager textureManager;
    MeshPipeline pipeline;
    // 視点の定数(b2)。別キューで描くので、本編のものは共有せず1枠だけ持つ。毎回フェンスで待ってから書く。
    ViewConstantsBuffer viewConstants;

    bool usable = false; // Init成功かつデバイスが生きている間だけtrue
};

} // namespace rs::render

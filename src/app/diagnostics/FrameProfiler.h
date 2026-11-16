//
// FrameProfiler.h - フレーム毎の計測(RS_PROFILE_FRAME)
//
// 実機Releaseビルドで .rs2 レイアウト表示中のフレーム時間の揺れを調べるための
// 診断クラス。vsync予算超過フレームとゲーム内固定tick実行回数の相関を後から
// CSVで分析できるようにする。環境変数RS_PROFILE_FRAMEが未設定なら完全に無効
// (記録もファイル出力もしない)。有効時もファイル書き込みは計測対象のヒッチに
// なるため毎フレーム行わず、メモリ上のリングバッファに溜めてFlush()で1回だけ
// 書き出す。
//
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "CpuRenderBreakdown.h"

namespace rs::app {

// 1フレーム分の計測レコード。
struct FrameProfileRecord {
    uint64_t frameIndex;
    double deltaTimeMs;
    int gameTicks;
    double sceneTickMs;
    double trainTickMs;
    double sceneryTickMs;
    double trainSearchMs;
    double trainKinematicsMs;
    double carPartWorldMs;
    double trainRuntimeMs;
    double trainSubmitMs;
    double carBatchMs;
    double carFallbackMs; // SubmitRenderSnapshotの個別(非バッチ)GPU提出経路の所要時間
    size_t recomputedSlots;
    double cpuFrameMs;  // フレームループ1イテレーション全体の所要時間(Present/GPU待ちを含む)
    double presentMs;   // GpuContext::Present()自体の所要時間
    double frameWaitMs; // MoveToNextFrame()でのフェンス待機時間(待たずに済んだフレームは0)
    // パス別GPU実時間(ms、DX12タイムスタンプクエリ計測。GpuFrameTimer.h参照)。
    // Init失敗時は全て0になる。
    double gpuFrameMs;     // フレーム全体
    double gpuOpaqueMs;    // 不透明パス
    double gpuTransMs;     // WBOIT透明パス(記録)
    double gpuCompositeMs; // WBOIT合成
    double gpuImguiMs;     // ImGui
    double gpuTonemapMs;   // トーンマップ(HDR→バックバッファ)
    double gpuAtmosphereMs; // 大気LUTの更新(太陽が動いたフレームだけ重い)
    double gpuIblMs;       // IBLの生成(太陽が1度以上動いたフレームだけ重い)
    double gpuShadowMs;    // シャドウパス(夜・影なしの間は0)
    double gpuBloomMs;     // ブルーム(掛けない間は0)
    double gpuFxaaMs;      // FXAA(掛けない間は0)
    double gpuPrepassMs;   // 主視点の深度プリパス(アンビエントオクルージョンが有効な間だけ。掛けない間は0)
    double gpuAoMs;        // アンビエントオクルージョン(GTAOとぼかし。掛けない間は0)
    double gpuMotionBlurMs; // モーションブラー(掛けない間は0)
    double gpuDepthOfFieldMs; // フォトモードの被写界深度(掛けない間は0)
    double gpuVolumetricFogMs; // ボリュメトリックフォグの格子(使わない間は0)
    CpuRenderBreakdown cpuRender; // 描画まわりのCPU区間の内訳
};

class FrameProfiler {
public:
    FrameProfiler();

    bool Enabled() const { return enabled_; }

    // frameIndexは内部カウンタで自動採番する。無効時は何もしない。
    // recomputedSlots: このフレーム中にSceneRenderer::Tick()が実際にパーツ行列を
    // 再計算したスロット数の合計(SceneRenderer::LastTickRecomputedSlots()、
    // 不変スキップの効き具合を確認するための計測用)。
    // cpuFrameMs/presentMs/frameWaitMs: 断続的なvblank落ち(4フレームに1回26.9ms等)の
    // 切り分け用。cpuFrameMsはフレームループ全体、presentMs/frameWaitMsはそのうち
    // GpuContext側でPresent呼び出し自体とGPU待ちに費やした内訳。
    void Record(double deltaTimeMs, int gameTicks, double sceneTickMs, double trainTickMs,
               double sceneryTickMs, double trainSearchMs, double trainKinematicsMs,
               double carPartWorldMs, double trainRuntimeMs, double trainSubmitMs,
               double carBatchMs, double carFallbackMs, size_t recomputedSlots,
               double cpuFrameMs, double presentMs, double frameWaitMs, double gpuFrameMs,
               double gpuOpaqueMs, double gpuTransMs, double gpuCompositeMs, double gpuImguiMs,
               double gpuTonemapMs, double gpuAtmosphereMs, double gpuIblMs,
               double gpuShadowMs, double gpuBloomMs, double gpuFxaaMs, double gpuPrepassMs,
               double gpuAoMs, double gpuMotionBlurMs, double gpuDepthOfFieldMs, double gpuVolumetricFogMs,
               const CpuRenderBreakdown& cpuRender);

    // 溜めたレコードをCSVへ書き出す。ヘッダ行
    // "frameIndex,deltaTimeMs,gameTicks,sceneTickMs,trainTickMs,sceneryTickMs,recomputedSlots,cpuFrameMs,presentMs,
    //  frameWaitMs,gpuFrameMs,gpuOpaqueMs,gpuTransMs,gpuCompositeMs,gpuImguiMs,gpuTonemapMs,gpuAtmosphereMs,gpuIblMs,gpuShadowMs,gpuBloomMs,gpuFxaaMs,gpuPrepassMs,gpuAoMs,gpuMotionBlurMs,gpuDepthOfFieldMs,gpuVolumetricFogMs,
    //  appRenderMs,beginFrameMs,environmentUpdateMs,rendererMs,cullMs,batchSyncMs,shadowCpuMs,meshRecordMs,overlay2dMs,rmlUiRecordMs,imguiRecordMs,endFrameMs,
    //  overlayPolylines,overlayPolylinePoints,overlayCircles,overlayRects,overlayTexts"
    // の後、1行1フレーム。
    // 二重出力防止のため2回目以降の呼び出しは何もしない。
    void Flush();

private:
    static constexpr size_t kMaxRecords = 32768; // 上限到達時は古いものを捨てるリング

    bool enabled_;
    std::string outputPath_;
    std::vector<FrameProfileRecord> records_;
    size_t nextSlot_; // リング上の次書き込み位置(records_.size() == kMaxRecords以降)
    uint64_t nextFrameIndex_;
    bool flushed_;
};

} // namespace rs::app

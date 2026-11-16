//
// CpuRenderBreakdown.h - 描画まわりのCPU区間の内訳(RS_PROFILE_FRAMEのCSV用)
//
// 全て単位ms。区間は入れ子になりうる(appRenderMsはrendererMsなどを含む)。
//
#pragma once

#include <chrono>

namespace rs::app {

struct CpuRenderBreakdown {
    double appRenderMs = 0.0;          // App::Render全体
    double beginFrameMs = 0.0;         // GpuContext::BeginFrame
    double environmentUpdateMs = 0.0;  // 大気LUT・IBLの更新の記録
    double rendererMs = 0.0;           // FrameRenderer::Render全体
    double cullMs = 0.0;               // スロットの視錐台カリング
    double batchSyncMs = 0.0;          // スロットのパーツ行列転送とインスタンスバッチの同期
    double shadowCpuMs = 0.0;          // 影の準備と影パスの記録
    double meshRecordMs = 0.0;         // メッシュ描画(不透明・半透明)の記録
    double overlay2dMs = 0.0;          // 2D注記の記録
    double rmlUiRecordMs = 0.0;        // RmlUiの記録
    double imguiRecordMs = 0.0;        // ImGuiの描画データ作成と記録
    double endFrameMs = 0.0;           // EndFrameとRmlUiのSubmitFrame
    // 2D注記の入力の件数(Overlay2DPassが毎フレーム頂点へ展開する量)。
    double overlayPolylines = 0.0;     // 折れ線の本数
    double overlayPolylinePoints = 0.0; // 折れ線の点の総数
    double overlayCircles = 0.0;
    double overlayRects = 0.0;
    double overlayTexts = 0.0;
};

// スコープの経過時間(ms)を target へ加算する。
class ScopedAccumulate {
public:
    explicit ScopedAccumulate(double& target)
        : target_(target), start_(std::chrono::steady_clock::now()) {}
    ~ScopedAccumulate() {
        target_ += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_).count();
    }
    ScopedAccumulate(const ScopedAccumulate&) = delete;
    ScopedAccumulate& operator=(const ScopedAccumulate&) = delete;

private:
    double& target_;
    std::chrono::steady_clock::time_point start_;
};

} // namespace rs::app

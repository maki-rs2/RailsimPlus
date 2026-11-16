#include "FrameRenderer.h"

#include <optional>

#include <spdlog/spdlog.h>

#include "App.h"
#include "GpuContext.h"

// FrameRenderer のフォトモードの ISO オートの測光(資源の管理とパスの呼び出し)。
// 資源は固定の大きさ(32x18 の格子と読み戻しのバッファ)で、最初に測光を使うときに1回だけ作る。測光を使わない間は何も作らず、
// パスも走らない。パスは FramePassTable.h の表の順に、レンズフレアの直後・ポストプロセスの前に走る。

namespace rs::app {

bool FrameRenderer::InitLuminanceMeter(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap) {
    if (!luminanceMeter_.Init(device, srvHeap, kSrvLayout.luminanceMeter)) {
        spdlog::warn("[luminance_meter] 測光を初期化できなかったため、ISO オートは使えません");
        return false;
    }
    return true;
}

void FrameRenderer::UpdateLuminanceMeterResources() {
    if (luminanceMeterEnabled_) luminanceMeter_.CreateResources();
}

void FrameRenderer::RecordLuminanceMeter(const rs::rhi::CommandList& commandList, rs::render::SceneHdrTarget& scene) {
    const ScopedAccumulate rendererScope(cpuBreakdown_.rendererMs);
    luminanceMeter_.Record(commandList, scene);
}

std::optional<rs::render::LuminanceMeterGrid> FrameRenderer::PollLuminanceMeter() {
    rs::render::LuminanceMeterGrid grid;
    if (!luminanceMeter_.Poll(grid)) return std::nullopt;
    return grid;
}

} // namespace rs::app

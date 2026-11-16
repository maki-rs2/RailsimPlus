#include "FrameRenderer.h"

#include <cmath>
#include <cstring>

#include <spdlog/spdlog.h>

#include "../../core/Math.h"
#include "../../render/postprocess/TaaMath.h"
#include "../../render/resource/RenderTuning.h"
#include "App.h"
#include "CameraMath.h"
#include "GpuContext.h"
#include "PassSizing.h"

// FrameRenderer の TAA(ジッタ・履歴の管理・解決パスの呼び出し)。
// 解決パスは FramePassTable.h の表の順に、シーンの記録の直後・レンズフレアの前に走る。

namespace rs::app {

bool FrameRenderer::InitTaa(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap) {
    if (!taa_.Init(device, srvHeap, kSrvLayout.taa)) {
        spdlog::warn("[taa] TAA を初期化できなかったため、使えません");
        return false;
    }
    return true;
}

void FrameRenderer::UpdateTaaResources() {
    if (!temporalAa_ || targetWidth_ == 0 || targetHeight_ == 0) return;
    const TargetSize size{targetWidth_, targetHeight_};
    if (!IsSizeReady(taa_, size)) {
        EnsureSize(taa_, size);
        taaHistoryValid_ = false; // 作り直した(中身が無い)
    }
    // 深度バッファは画面の大きさの変更で作り直されるので、そのつど SRV を書き直す。
    meshPipeline_.WriteDepthSrv(mainSrvHeap_, taa_.DepthSrvIndex());
}

void FrameRenderer::PrepareTaaView(const CameraView& cam, const ViewProjResult& viewProj, UINT width, UINT height,
                                   rs::render::ViewConstants& mainView) {
    // 前フレームの viewProj。初回と画面の大きさを変えた直後は「前 = 今」(MakeViewConstants が入れた値のまま)。
    if (prevViewProjValid_) std::memcpy(mainView.prevViewProj, prevViewProj_, sizeof(prevViewProj_));

    const rs::render::RenderTuning& tuning = rs::render::RenderTuning::Embedded();
    // TAA の解決・モーションブラー・被写界深度の入力。ジッタをかける前の(ジッタなしの)行列で、今と前フレームの再投影を求める。
    // 視点が大きく動いた(瞬間移動)ら、前フレームは別の景色なので、履歴もぼかしも使わない。
    const float dx = cam.eye[0] - prevEye_[0];
    const float dy = cam.eye[1] - prevEye_[1];
    const float dz = cam.eye[2] - prevEye_[2];
    viewJumped_ = prevEyeValid_ && std::sqrt(dx * dx + dy * dy + dz * dz) > tuning.taaCutDistanceM;
    if (temporalAa_ || motionBlurEnabled_ || DepthOfFieldEnabled()) {
        taaFrameOk_ = rs::core::InvertMat4(viewProj.viewProj, taaInvViewProj_);
        std::memcpy(framePrevViewProj_, mainView.prevViewProj, sizeof(framePrevViewProj_));
    }
    if (temporalAa_) {
        std::memcpy(taaFrame_.invViewProj, taaInvViewProj_.m, sizeof(taaFrame_.invViewProj));
        std::memcpy(taaFrame_.prevViewProj, mainView.prevViewProj, sizeof(taaFrame_.prevViewProj));
        const rs::render::TaaJitterNdc jitter = rs::render::TaaJitterToNdc(
            rs::render::TaaJitterForFrame(static_cast<std::uint32_t>(frameCounter_)), width, height);
        taaFrame_.jitterNdc[0] = jitter.x;
        taaFrame_.jitterNdc[1] = jitter.y;
        taaFrame_.historyWeight = tuning.taaHistoryWeight;
        taaFrame_.sharpness = tuning.taaSharpness;
        taaFrame_.velocitySrvIndex = kSrvLayout.velocity;
        if (viewJumped_) taaHistoryValid_ = false;
        // 主視点の viewProj だけにジッタをかける。メッシュを描く全部がこの値(b2)を読むので、ずれが揃う。
        rs::render::ApplyNdcJitter(mainView.viewProj, jitter);
    }
    std::memcpy(prevViewProj_, viewProj.viewProj.m, sizeof(prevViewProj_));
    prevViewProjValid_ = true;
    std::memcpy(prevEye_, cam.eye, sizeof(prevEye_));
    prevEyeValid_ = true;
}

bool FrameRenderer::TemporalAaReady() const {
    return temporalAa_ && taaFrameOk_ && IsSizeReady(taa_, TargetSize{targetWidth_, targetHeight_});
}

rs::render::SceneHdrTarget* FrameRenderer::ResolveTaa(const rs::rhi::CommandList& commandList,
                                                      rs::render::SceneHdrTarget& hdr) {
    const ScopedAccumulate rendererScope(cpuBreakdown_.rendererMs);
    taaFrame_.resetHistory = !taaHistoryValid_;
    meshPipeline_.TransitionDepth(commandList, true); // 解決が深度を読む間だけ読み取り用にする
    taa_.Record(commandList, hdr, taaFrame_);
    meshPipeline_.TransitionDepth(commandList, false);
    taaHistoryValid_ = true;
    return &taa_.Resolved();
}

} // namespace rs::app

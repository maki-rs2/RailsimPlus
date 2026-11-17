#include "FrameRenderer.h"

#include <chrono>
#include <cstring>
#include <optional>

#include <spdlog/spdlog.h>

#include "../../render/pipeline/EnvFlag.h"
#include "../../render/postprocess/MotionBlurMath.h"
#include "../../render/resource/RenderTuning.h"
#include "App.h"
#include "GpuContext.h"
#include "PassSizing.h"

// FrameRenderer のモーションブラー(資源の管理とパスの呼び出し)。
// 速度の元(深度プリパス・物体の速度バッファ・前フレームの viewProj)は TAA と同じものを使う(FrameRendererTaa.cpp の
// PrepareTaaView が両方の入力を作る)。パスは FramePassTable.h の表の順に、TAA の解決の直後・レンズフレアの前に走る。

namespace rs::app {

namespace {

// 開発用: 環境変数 RS_MB_SHUTTER(数値)があれば、調整値のシャッターの割合より優先する(ヘッドレスの確認で、ぼかしを大きく
// 出して見るため。ゲームの設定には出さない)。
std::optional<float> ShutterOverride() {
    static const std::optional<float> kValue = rs::render::ReadEnvFloat("RS_MB_SHUTTER");
    return kValue;
}

} // namespace

bool FrameRenderer::InitMotionBlur(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap) {
    if (!motionBlur_.Init(device, srvHeap, kSrvLayout.motionBlur)) {
        spdlog::warn("[motion_blur] モーションブラーを初期化できなかったため、使えません");
        return false;
    }
    return true;
}

void FrameRenderer::UpdateMotionBlurResources() {
    if (!motionBlurEnabled_ || targetWidth_ == 0 || targetHeight_ == 0) return;
    EnsureSize(motionBlur_, TargetSize{targetWidth_, targetHeight_});
    // 深度バッファは画面の大きさの変更で作り直されるので、そのつど SRV を書き直す。
    meshPipeline_.WriteDepthSrv(mainSrvHeap_, motionBlur_.DepthSrvIndex());
}

void FrameRenderer::UpdateMotionBlurClock() {
    // 前回の呼び出しからの時間。ぼかさないフレームも含めて毎回更新し、初回(時刻が未設定)は基準のフレーム時間とみなす。
    const auto now = std::chrono::steady_clock::now();
    frame_.motionBlurFrameSeconds =
        lastMotionBlurTime_.time_since_epoch().count() == 0
            ? rs::render::kMotionBlurReferenceFrameSeconds
            : std::chrono::duration<float>(now - lastMotionBlurTime_).count();
    lastMotionBlurTime_ = now;
}

bool FrameRenderer::MotionBlurReady() const {
    // 視点が瞬間移動した・シャッターが0・入力の逆行列が無い・資源が無いときは、ぼかさない(前フレームとの動きが求まらない)。
    const rs::render::RenderTuning& tuning = rs::render::RenderTuning::Embedded();
    return motionBlurEnabled_ && ShutterOverride().value_or(tuning.motionBlurShutter) > 0.0f && !viewJumped_ &&
           taaFrameOk_ && IsSizeReady(motionBlur_, TargetSize{targetWidth_, targetHeight_});
}

rs::render::SceneHdrTarget* FrameRenderer::ResolveMotionBlur(const rs::rhi::CommandList& commandList,
                                                             rs::render::SceneHdrTarget& scene,
                                                             GpuFrameTimer* timer) {
    const ScopedAccumulate rendererScope(cpuBreakdown_.rendererMs);
    const rs::render::RenderTuning& tuning = rs::render::RenderTuning::Embedded();
    rs::render::MotionBlurFrameParams params;
    std::memcpy(params.invViewProj, taaInvViewProj_.m, sizeof(params.invViewProj));
    std::memcpy(params.prevViewProj, framePrevViewProj_, sizeof(params.prevViewProj));
    if (photoShutterSeconds_) {
        // ファインダー中は、撮影カメラのシャッターが開いている間の動きをぼかしの長さにする。
        params.shutter = rs::render::MotionBlurShutterForExposure(*photoShutterSeconds_, frame_.motionBlurFrameSeconds);
        params.maxRadiusFraction = tuning.photoMotionBlurMaxRadius;
    } else {
        params.shutter = rs::render::MotionBlurShutterForFrameTime(ShutterOverride().value_or(tuning.motionBlurShutter),
                                                                   frame_.motionBlurFrameSeconds);
        params.maxRadiusFraction = tuning.motionBlurMaxRadius;
    }
    params.samples = tuning.motionBlurSamples;
    params.frameIndex = static_cast<std::uint32_t>(frameCounter_);
    params.objectVelocitySrvIndex = kSrvLayout.velocity;
    if (timer) timer->Begin(commandList.Native(), kGpuRegionMotionBlur);
    meshPipeline_.TransitionDepth(commandList, true); // ぼかしが深度を読む間だけ読み取り用にする
    motionBlur_.Record(commandList, scene, params);
    meshPipeline_.TransitionDepth(commandList, false);
    if (timer) timer->End(commandList.Native(), kGpuRegionMotionBlur);
    return &motionBlur_.Output();
}

} // namespace rs::app

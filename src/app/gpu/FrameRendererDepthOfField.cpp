#include "FrameRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <optional>

#include <spdlog/spdlog.h>

#include "../../platform/EnvironmentVariable.h"
#include "../../render/postprocess/DepthOfFieldMath.h"
#include "../../render/resource/RenderTuning.h"
#include "../camera/PhotoCamera.h"
#include "App.h"
#include "GpuContext.h"
#include "PassSizing.h"

// FrameRenderer のフォトモードの被写界深度(資源の管理とパスの呼び出し)。
// 本編の深度と今フレームの viewProj の逆行列(PrepareTaaView が作る)だけを使い、速度・プリパスは使わない。
// パスは FramePassTable.h の表の順に、TAA の解決の直後・モーションブラーの前に走る。

namespace rs::app {

namespace {

bool IsUsable(const rs::render::DepthOfFieldCamera& camera) {
    return std::isfinite(camera.focalLengthMm) && camera.focalLengthMm > 0.0 && std::isfinite(camera.aperture) &&
           camera.aperture > 0.0 && std::isfinite(camera.focusDistanceM) && camera.focusDistanceM > 0.0;
}

// 開発用: 環境変数 RS_DOF=<焦点距離mm>,<絞り>,<フォーカス距離m> があれば、ファインダーが出ていなくても被写界深度を
// 掛ける(ヘッドレスの確認で、ぼけを描いて見るため。ゲームの設定には出さない)。読めない値は無視する。
std::optional<rs::render::DepthOfFieldCamera> EnvOverride() {
    static const std::optional<rs::render::DepthOfFieldCamera> kValue = []() -> std::optional<rs::render::DepthOfFieldCamera> {
        const char* text = rs::platform::ReadEnvironmentVariable("RS_DOF");
        if (text == nullptr) return std::nullopt;
        double values[3] = {};
        const char* cursor = text;
        for (int i = 0; i < 3; ++i) {
            char* end = nullptr;
            values[i] = std::strtod(cursor, &end);
            if (end == cursor || (i < 2 && *end != ',')) {
                spdlog::warn("[depth_of_field] RS_DOF は <焦点距離mm>,<絞り>,<フォーカス距離m> の形で指定します: {}", text);
                return std::nullopt;
            }
            cursor = (i < 2) ? end + 1 : end;
        }
        const rs::render::DepthOfFieldCamera camera{values[0], values[1], values[2]};
        if (!IsUsable(camera)) {
            spdlog::warn("[depth_of_field] RS_DOF の値が正の有限値ではありません: {}", text);
            return std::nullopt;
        }
        return camera;
    }();
    return kValue;
}

} // namespace

bool FrameRenderer::InitDepthOfField(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap) {
    if (!depthOfField_.Init(device, srvHeap, kSrvLayout.depthOfField)) {
        spdlog::warn("[depth_of_field] 被写界深度を初期化できなかったため、使えません");
        return false;
    }
    return true;
}

void FrameRenderer::SetDepthOfFieldCamera(const std::optional<rs::render::DepthOfFieldCamera>& camera) {
    depthOfFieldCamera_ = camera.has_value() ? camera : EnvOverride();
}

void FrameRenderer::RequestDepthProbe(double u, double v) {
    if (depthProbe_.requested) return; // 結果が出るまで次の要求は受けない
    depthProbe_.requested = true;
    depthProbe_.u = u;
    depthProbe_.v = v;
}

std::optional<double> FrameRenderer::PollDepthProbe() {
    if (!depthProbe_.recorded) return std::nullopt;
    float depth = 0.0f;
    if (!meshPipeline_.PollDepthProbe(depth)) return std::nullopt;
    const double ndcX = (depthProbe_.x + 0.5) / depthProbe_.width * 2.0 - 1.0;
    const double ndcY = 1.0 - (depthProbe_.y + 0.5) / depthProbe_.height * 2.0;
    const double meters = rs::render::LinearDepthM(depthProbe_.invViewProj, ndcX, ndcY, depth);
    depthProbe_ = {};
    return meters;
}

void FrameRenderer::UpdateDepthOfFieldResources() {
    if (!DepthOfFieldEnabled() || targetWidth_ == 0 || targetHeight_ == 0) return;
    EnsureSize(depthOfField_, TargetSize{targetWidth_, targetHeight_});
    // 深度バッファは画面の大きさの変更で作り直されるので、そのつど SRV を書き直す。
    meshPipeline_.WriteDepthSrv(mainSrvHeap_, depthOfField_.DepthSrvIndex());
}

bool FrameRenderer::DepthOfFieldReady() const {
    // 撮影カメラの値が無い・不正、入力の逆行列が無い、資源が無いときは掛けない。
    return DepthOfFieldEnabled() && IsUsable(*depthOfFieldCamera_) && taaFrameOk_ &&
           IsSizeReady(depthOfField_, TargetSize{targetWidth_, targetHeight_});
}

rs::render::SceneHdrTarget* FrameRenderer::ResolveDepthOfField(const rs::rhi::CommandList& commandList,
                                                               rs::render::SceneHdrTarget& scene,
                                                               GpuFrameTimer* timer) {
    const ScopedAccumulate rendererScope(cpuBreakdown_.rendererMs);
    const rs::render::RenderTuning& tuning = rs::render::RenderTuning::Embedded();
    const rs::render::DepthOfFieldCamera& camera = *depthOfFieldCamera_;
    rs::render::DepthOfFieldFrameParams params;
    std::memcpy(params.invViewProj, taaInvViewProj_.m, sizeof(params.invViewProj));
    params.focalLengthMm = static_cast<float>(camera.focalLengthMm);
    params.aperture = static_cast<float>(camera.aperture);
    params.focusDistanceM =
        static_cast<float>(rs::render::ClampFocusDistanceM(camera.focalLengthMm, camera.focusDistanceM));
    // 撮影フレームの幅は、描いた縦横比(ファインダーの枠に合わせた縦横比)で決まる。
    params.frameWidthMm = static_cast<float>(
        PhotoFrameForAspect(EffectiveAspect(frame_.cam, frame_.width, frame_.height)).widthMm);
    params.maxRadiusFraction = tuning.dofMaxRadius;
    params.samples = tuning.dofSamples;
    params.frameIndex = static_cast<std::uint32_t>(frameCounter_);
    if (timer) timer->Begin(commandList.Native(), kGpuRegionDepthOfField);
    if (depthProbe_.requested && !depthProbe_.recorded) {
        const unsigned x = std::min(static_cast<unsigned>(std::max(depthProbe_.u, 0.0) * targetWidth_), targetWidth_ - 1);
        const unsigned y = std::min(static_cast<unsigned>(std::max(depthProbe_.v, 0.0) * targetHeight_), targetHeight_ - 1);
        if (meshPipeline_.RecordDepthProbe(commandList, x, y)) {
            depthProbe_.recorded = true;
            depthProbe_.x = x;
            depthProbe_.y = y;
            depthProbe_.width = targetWidth_;
            depthProbe_.height = targetHeight_;
            std::memcpy(depthProbe_.invViewProj, taaInvViewProj_.m, sizeof(depthProbe_.invViewProj));
        }
    }
    meshPipeline_.TransitionDepth(commandList, true); // ぼかしが深度を読む間だけ読み取り用にする
    depthOfField_.Record(commandList, scene, params);
    meshPipeline_.TransitionDepth(commandList, false);
    if (timer) timer->End(commandList.Native(), kGpuRegionDepthOfField);
    return &depthOfField_.Output();
}

} // namespace rs::app

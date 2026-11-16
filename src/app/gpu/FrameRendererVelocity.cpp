#include "FrameRenderer.h"

#include <spdlog/spdlog.h>

#include "../../render/pipeline/MeshConstants.h"
#include "App.h"
#include "GpuContext.h"
#include "PassSizing.h"

// FrameRenderer の速度パスと、深度プリパス・速度バッファなど画面の大きさに依存する資源の用意。
// 速度パスは深度プリパスの直後に、動いているパーツだけを速度バッファへ書く。
// 書くのは物体の動きだけで、カメラの動きは含めない(動かない画素は0。カメラの分は後段が深度と前フレームの viewProj から求める)。

namespace rs::app {

bool FrameRenderer::InitPrepassTargets(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap) {
    // 深度の SRV は、アンビエントオクルージョンの SRV 枠の先頭(AmbientOcclusionPass::kSrvCount 参照)。
    const bool depthOk = prepassDepth_.Init(device, srvHeap, kSrvLayout.ambientOcclusion);
    if (!depthOk) spdlog::warn("[prepass] 深度プリパスの出力を初期化できなかったため、AO と TAA を使えません");
    const float zero[3] = {0.0f, 0.0f, 0.0f};
    const bool velocityOk = velocity_.Init(device, srvHeap, kSrvLayout.velocity, zero, rs::render::HdrTargetFormat::Rg16Float);
    if (!velocityOk) spdlog::warn("[velocity] 速度バッファを初期化できなかったため、速度を使えません");
    return depthOk && velocityOk;
}

bool FrameRenderer::VelocityWanted() const { return PrepassEffectActive() || rs::render::VelocityDebug::Enabled(); }

void FrameRenderer::SetTemporalAa(bool enabled) {
    temporalAa_ = enabled;
    taaHistoryValid_ = false; // 切り替えた直後は履歴が無い(または古い)
}

void FrameRenderer::EnsureSizedResources() {
    const UINT width = targetWidth_;
    const UINT height = targetHeight_;
    if (width == 0 || height == 0) return;
    const TargetSize size{width, height};
    // 深度プリパスは、AO を掛ける・速度を使うとき(PrepareMeshFrame のプリパスの条件と同じ)に要る。
    const bool needDepth = aoQuality_ != rs::render::AoQuality::None || VelocityWanted();
    if (needDepth) EnsureSize(prepassDepth_, size);
    if (VelocityWanted()) EnsureSize(velocity_, size);
    EnsureSize(ambientOcclusion_, size, aoQuality_); // 品質が「なし」なら何もしない
    UpdateTaaResources();
    UpdateMotionBlurResources();
    UpdateDepthOfFieldResources();
    UpdateLuminanceMeterResources();
    UpdateVolumetricFogResources();
}

void FrameRenderer::RecordVelocity(const rs::rhi::CommandList& commandList, GpuFrameTimer* timer) {
    const ScopedAccumulate rendererScope(cpuBreakdown_.rendererMs);
    if (timer) timer->Begin(commandList.Native(), kGpuRegionVelocity);
    // 速度バッファを0でクリアして出力先にし、深度はプリパスのものを読み取り専用で張る。
    velocity_.BeginScene(commandList, rs::rhi::CpuDescriptor{});
    prepassDepth_.BindWithColor(commandList, velocity_.Rtv());
    for (const rs::render::MeshDrawEntry& entry : drawEntries_) {
        if (entry.fadeAlpha < 1.0f) continue;
        entry.pipeline->RecordVelocityInstancedMultiPart(commandList, frame_.constants, frame_.frameIndex, 1,
                                                         static_cast<UINT>(entry.pipeline->PartXforms().size()));
    }
    // 静的インスタンス(InstanceBatchManager)は動かないので描かない。
    if (frame_.anyTrainInstanceBatch) trainInstanceBatches_.RecordVelocity(commandList, frame_.constants, frame_.frameIndex);
    velocity_.Transition(commandList, rs::render::HdrTargetState::ShaderResource);
    velocityDebug_.Step(commandList, velocity_);
    if (timer) timer->End(commandList.Native(), kGpuRegionVelocity);
}

} // namespace rs::app

#include "FrameRenderer.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <spdlog/spdlog.h>

#include "imgui.h"

#include "../../platform/font/UiFont.h"
#include "../../render/pipeline/MeshFrameRecord.h"
#include "../../render/postprocess/PostProcessFormats.h"
#include "../../render/sky/EnvironmentState.h"
#include "../../render/resource/RenderTuning.h"
#include "../../render/resource/XLoader.h"
#include "../../core/Math.h"
#include "App.h"
#include "CameraMath.h"
#include "GpuContext.h"

namespace rs::app {

using rs::core::Normalize3;

namespace {
// 配置プレビュー(情景・編成)の不透明度。ワープのフェード中の車両と同じ方式
// (不透明submeshは深度EQUALブレンド、元から半透明のsubmeshはWBOITでαに掛ける)で描く。
constexpr float kPlacementPreviewAlpha = 0.45f;
} // namespace

static_assert(rs::render::ParticlePass::kRingSize == kFrameCount);

void FrameRenderer::RecordParticles(const rs::rhi::CommandList& commandList, rs::rhi::CpuDescriptor rtv,
                                    std::uint32_t frameIndex) {
    const ScopedAccumulate rendererScope(cpuBreakdown_.rendererMs);
    if (!particlesEnabled_ || particleBatch_.Instances().empty()) return;
    rs::render::ParticleFrame frame;
    frame.view = frame_.viewProj.view;
    // 粒の位置はカメラ相対なので、ビューの平行移動を二重に掛けない。
    frame.view.m[12] = frame.view.m[13] = frame.view.m[14] = 0.0f;
    frame.viewProj = rs::core::Mat4::Mul(frame.view, frame_.viewProj.proj);
    frame.instances = particleBatch_.Instances();
    frame.width = frame_.width;
    frame.height = frame_.height;
    frame.frameIndex = frameIndex;
    frame.depthProjection[0] = frame_.viewProj.proj.m[10];
    frame.depthProjection[1] = frame_.viewProj.proj.m[14];
    const rs::render::MeshConstants& constants = frame_.constants;
    frame.iblDiffuseIntensity = constants.iblParams[1];
    std::memcpy(frame.lightDir, constants.lightDir, sizeof(frame.lightDir));
    std::memcpy(frame.pluginDirectional, constants.pluginDirectional, sizeof(frame.pluginDirectional));
    std::memcpy(frame.pluginAmbient, constants.pluginAmbient, sizeof(frame.pluginAmbient));
    std::memcpy(frame.moonLight, constants.moonLight, sizeof(frame.moonLight));
    frame.diffuseCubeSrvIndex = constants.iblIndex[0];
    frame.transmittanceSrvIndex = constants.iblIndex[3];
    frame.iblEnabled = constants.iblFlags[0] != 0u;
    std::memcpy(frame.adaptation, constants.adaptation, sizeof(frame.adaptation));
    meshPipeline_.TransitionDepth(commandList, true);
    particlePass_.Record(commandList, rtv, rs::rhi::CpuDescriptor{meshPipeline_.DepthDsvReadOnly().ptr}, frame);
    meshPipeline_.TransitionDepth(commandList, false);
}


// FrameRenderer の描画コマンド記録(2D注記・ハイライト・WBOIT/ソート/後段不透明の各パス)。

void FrameRenderer::RecordOverlay2D(ID3D12GraphicsCommandList* cmdList,
                                    D3D12_CPU_DESCRIPTOR_HANDLE rtv,
                                    const rs::core::DebugDrawList& list,
                                    const CameraView& cam, UINT width, UINT height,
                                    UINT frameIndex) {
    const ViewProjResult viewProj = BuildViewProj(cam, width, height);
    overlay2DPass_.Record(cmdList, rtv, meshPipeline_.DepthDsvReadOnly(), list,
                          viewProj.viewProj, width, height, frameIndex);
}

bool FrameRenderer::IsSceneSlotVisible(size_t slot) const {
    return slot >= sceneSlotState_.Visible().size() || sceneSlotState_.Visible()[slot];
}

bool FrameRenderer::IsSceneSlotDrawable(size_t slot) const {
    return scenePipelines_[slot].HasMesh() && IsSceneSlotVisible(slot) &&
           (slot >= sceneSlotState_.Culled().size() || sceneSlotState_.Culled()[slot] == 0);
}

// Scenery選択ハイライト: 全不透明パスの直後、
// まだHDRシーンターゲット+深度がバインドされている時点で描く必要がある
// (WboitPass::Compositeはシーンターゲットのみ・深度nullptrでOMSetRenderTargets
// するため、Composite後に描くと深度テストが機能しない)。
// occluded部分はGREATER_EQUAL深度テストで自然に隠れる(x線透視にならない)。
void FrameRenderer::RecordHighlights(ID3D12GraphicsCommandList* cmdList,
                                     const rs::render::MeshConstants& c, UINT fc) {
    for (UINT i = 0; i < kHighlightSlots; ++i) {
        if (!highlightState_.Active(i)) continue;
        rs::render::MeshPipeline* highlightPipeline = highlightState_.Pipeline(i);
        const rs::core::Mat4* highlightWorld = highlightState_.World(i);
        const float* highlightTint = highlightState_.Tint(i);
        if (highlightPipeline == nullptr || highlightWorld == nullptr || highlightTint == nullptr ||
            !highlightPipeline->HasMesh()) continue;
        rs::render::PartXform px{};
        std::memcpy(px.world, highlightWorld->m, sizeof(px.world));
        std::memcpy(px.normalMat, highlightWorld->m, sizeof(px.normalMat));
        px.normalMat[12] = px.normalMat[13] = px.normalMat[14] = 0.0f;
        highlightPipeline->UpdatePartWorlds({px}, fc);
        highlightPipeline->RecordHighlightInstanced(cmdList, c, fc, highlightTint);
    }
    // 非静的Scenery(slots_側): 自身のパイプライン(gPartWorldは
    // SceneRenderer::Tickが毎フレーム最新化済み)をそのまま再利用して重ね描く。
    for (UINT i = 0; i < kHighlightSlots; ++i) {
        if (!highlightState_.SlotHighlightActive(i)) continue;
        const UINT slot = highlightState_.SlotHighlightSlot(i);
        const float* slotHighlightTint = highlightState_.SlotHighlightTint(i);
        if (slotHighlightTint == nullptr) continue;
        if (slot >= scenePipelines_.size() || !IsSceneSlotDrawable(slot)) continue;
        scenePipelines_[slot].RecordHighlightSelf(cmdList, c, fc, slotHighlightTint);
    }
}

void FrameRenderer::BuildDrawEntries() {
    drawEntries_.clear();
    if (meshPipeline_.HasMesh()) drawEntries_.push_back({&meshPipeline_, 1.0f});
    for (size_t i = 0; i < scenePipelines_.size(); ++i)
        if (IsSceneSlotDrawable(i))
            drawEntries_.push_back({&scenePipelines_[i], sceneSlotState_.FadeAlpha()[i]});
}

// 描く枠(drawEntries_)を共通の記録順(rs::render::RecordMeshFrame)で描き、本編だけが持つ物
// (静的インスタンス・配置プレビュー・ハイライト・GPU計測の区切り)を各段階に差し込む。
// WBOITが使えなければ、半透明はソート版(通常のブレンド)になり、インスタンス配置の半透明は描かれない。
void FrameRenderer::RecordMeshPasses(const rs::rhi::CommandList& commandList, GpuFrameTimer* timer) {
    ID3D12GraphicsCommandList* cmdList = commandList.Native();
    const rs::render::MeshConstants& c = frame_.constants;
    const UINT fc = frame_.frameIndex;
    const bool anyInstanceBatch = frame_.anyInstanceBatch;
    const bool anyTrainInstanceBatch = frame_.anyTrainInstanceBatch;
    const rs::rhi::CpuDescriptor rtv = frame_.rtv;
    const bool wboit = wboit_.Ready() && sceneHdrTarget_ != nullptr;
    rs::render::MeshPipeline& previewPipeline = placementPreviewState_.Pipeline();
    const bool drawPreview = placementPreviewState_.Active() && previewPipeline.HasMesh();
    const UINT previewParts = static_cast<UINT>(previewPipeline.PartXforms().size());
    rs::render::MeshConstants previewConstants = c;
    previewConstants.material[3] = kPlacementPreviewAlpha;

    rs::render::MeshFrameTargets targets;
    targets.wboit = &wboit_;
    targets.depthDsv = rs::rhi::CpuDescriptor{meshPipeline_.DepthDsv().ptr};
    targets.compositeRtv = rtv;
    targets.compositeTarget = sceneHdrTarget_;

    if (timer) timer->Begin(cmdList, kGpuRegionOpaque);
    rs::render::RecordMeshFrame(
        commandList, c, fc, drawEntries_, targets, [&](rs::render::MeshFrameStage stage) {
            switch (stage) {
            case rs::render::MeshFrameStage::OpaqueEntries:
                if (drawPreview) previewPipeline.RecordFadeInstancedMultiPart(cmdList, previewConstants, fc, 1, previewParts);
                if (anyInstanceBatch) instanceBatches_.RecordOpaque(cmdList, c, fc);
                if (anyTrainInstanceBatch) trainInstanceBatches_.RecordOpaque(cmdList, c, fc);
                RecordVolumetricFogSky(commandList, c);
                break;
            case rs::render::MeshFrameStage::BlendedEntries:
                if (anyInstanceBatch) instanceBatches_.RecordBlended(cmdList, c, fc);
                if (anyTrainInstanceBatch) trainInstanceBatches_.RecordBlended(cmdList, c, fc);
                break;
            case rs::render::MeshFrameStage::LateOpaqueEntries:
                if (anyInstanceBatch) instanceBatches_.RecordLateOpaque(cmdList, c, fc);
                if (anyTrainInstanceBatch) trainInstanceBatches_.RecordLateOpaque(cmdList, c, fc);
                RecordHighlights(cmdList, c, fc);
                if (timer) timer->End(cmdList, kGpuRegionOpaque);
                if (timer && wboit) timer->Begin(cmdList, kGpuRegionTransparent);
                break;
            case rs::render::MeshFrameStage::TransparentEntries:
                if (!wboit) break;
                // 元から半透明のsubmeshだけをここで描く(プレビューの不透明submeshは上のフェード描画で済み)。
                if (drawPreview)
                    previewPipeline.RecordTransparentInstancedMultiPart(cmdList, previewConstants, fc, 1, previewParts, true);
                // 静的インスタンス配置の半透明submeshはWBOITへ蓄積する。順序非依存なので、
                // インスタンス化(任意順1ドロー)と両立し、per-instanceソート無しで正しく合成される。
                if (anyInstanceBatch) instanceBatches_.RecordTransparent(cmdList, c, fc);
                if (anyTrainInstanceBatch) trainInstanceBatches_.RecordTransparent(cmdList, c, fc);
                if (timer) timer->End(cmdList, kGpuRegionTransparent);
                if (timer) timer->Begin(cmdList, kGpuRegionComposite);
                break;
            }
        });
    if (timer && wboit) timer->End(cmdList, kGpuRegionComposite);
}

} // namespace rs::app

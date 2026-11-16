#include "FrameRenderer.h"

#include <cmath>

#include "../../render/pipeline/MeshConstants.h"
#include "../../render/resource/RenderTuning.h"
#include "CameraMath.h"
#include "GpuContext.h"

// FrameRenderer の主視点の深度プリパス・アンビエントオクルージョン(GTAO → ぼかし)と、出力先の張り直し。
// 本編の深度バッファ・PSO・描画には触れず、AO の結果(R8)をメッシュのシェーダが引く値を MeshConstants へ入れる。

namespace rs::app {

void FrameRenderer::RecordPrepass(const rs::rhi::CommandList& commandList, GpuFrameTimer* timer) {
    const ScopedAccumulate rendererScope(cpuBreakdown_.rendererMs);
    ID3D12GraphicsCommandList* cmdList = commandList.Native();
    // 主視点の深度プリパス。本編が深度を書く枠だけ(フェード中の枠・配置プレビュー・ハイライトは含めない)。
    if (timer) timer->Begin(cmdList, kGpuRegionPrepass);
    prepassDepth_.Begin(commandList);
    for (const rs::render::MeshDrawEntry& entry : drawEntries_) {
        if (entry.fadeAlpha < 1.0f) continue;
        entry.pipeline->RecordPrepassInstancedMultiPart(commandList, frame_.constants, frame_.frameIndex, 1,
                                                        static_cast<UINT>(entry.pipeline->PartXforms().size()));
    }
    if (frame_.anyInstanceBatch) instanceBatches_.RecordPrepass(commandList, frame_.constants, frame_.frameIndex);
    if (frame_.anyTrainInstanceBatch) trainInstanceBatches_.RecordPrepass(commandList, frame_.constants, frame_.frameIndex);
    if (timer) timer->End(cmdList, kGpuRegionPrepass);
}

void FrameRenderer::RecordAmbientOcclusion(const rs::rhi::CommandList& commandList, GpuFrameTimer* timer) {
    const ScopedAccumulate rendererScope(cpuBreakdown_.rendererMs);
    ID3D12GraphicsCommandList* cmdList = commandList.Native();
    if (timer) timer->Begin(cmdList, kGpuRegionAo);
    const rs::render::RenderTuning& tuning = rs::render::RenderTuning::Embedded();
    rs::render::AoFrameParams params;
    params.tanHalfY = std::tan(frame_.cam.fovYRadians * 0.5f);
    params.tanHalfX = params.tanHalfY * static_cast<float>(frame_.width) / static_cast<float>(frame_.height);
    params.nearZ = kCameraNearM;
    params.farZ = frame_.cam.renderDistance;
    params.radius = tuning.aoRadius;
    params.falloffRange = tuning.aoFalloffRange;
    params.finalPower = tuning.aoFinalPower;
    ambientOcclusion_.Record(commandList, prepassDepth_, params);
    if (timer) timer->End(cmdList, kGpuRegionAo);
    rs::render::ApplyAmbientOcclusionToMeshConstants(frame_.constants, ambientOcclusion_.ResultSrvIndex(), frame_.width,
                                                     frame_.height);
}

void FrameRenderer::RestoreMainTargets(const rs::rhi::CommandList& commandList) const {
    ID3D12GraphicsCommandList* cmdList = commandList.Native();
    const D3D12_CPU_DESCRIPTOR_HANDLE depthDsv = DepthDsv();
    const D3D12_CPU_DESCRIPTOR_HANDLE sceneRtv = {frame_.rtv.ptr};
    cmdList->OMSetRenderTargets(1, &sceneRtv, FALSE, &depthDsv);
    const D3D12_VIEWPORT viewport = {0.0f, 0.0f, static_cast<float>(frame_.width), static_cast<float>(frame_.height),
                                     0.0f, 1.0f};
    const D3D12_RECT scissor = {0, 0, static_cast<LONG>(frame_.width), static_cast<LONG>(frame_.height)};
    cmdList->RSSetViewports(1, &viewport);
    cmdList->RSSetScissorRects(1, &scissor);
}

void FrameRenderer::ResetMainTargets(const rs::rhi::CommandList& commandList) { RestoreMainTargets(commandList); }

} // namespace rs::app

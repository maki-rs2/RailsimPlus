// MeshPipelineRecordVelocity.cpp - MeshPipeline の速度パス(動いているパーツの速度を RT へ書く)
// 描く submesh と記録の流れは深度プリパスと同じ(RecordDepthOnlyDraws)。違いは PSO と、前フレーム分の行列の先頭を渡すことだけ。
#include "MeshPipeline.h"

#include "SubmeshAlphaClassify.h"
#include "../shadow/CascadeFit.h"

namespace rs::render {

void MeshPipeline::RecordVelocityInstancedMultiPart(const rs::rhi::CommandList& commandList,
                                                    const MeshConstants& constants, UINT frameIndex,
                                                    UINT instanceCount, UINT partsPerInstance) {
    if (geometry_.IndexCount() == 0 || instanceCount == 0 || partsPerInstance == 0) return;
    const UINT f = frameIndex % kFrames;
    // 前フレーム分の先頭は、この枠に最後に書いた内容のもの。0 なら動いているパーツが無い。
    const UINT prevOffset = dynamicBuffers_.PrevPartWorldOffset(f);
    if (prevOffset == 0) return;
    const UINT drawCount = CountPrepassDraws(geometry_.Submeshes());
    if (drawCount == 0) return;
    // プリパスのスロット([firstSlot, firstSlot+drawCount))の後ろを使う。同じ枠を使うと、先に記録したプリパスが
    // 後から書かれた定数を読む。バッファの確保はプリパス側(RecordPrepassInstancedMultiPart)が両方ぶん済ませている。
    const UINT firstSlot = CountShadowCasters(geometry_.Submeshes()) * static_cast<UINT>(kMaxShadowCascades);
    if (!EnsureShadowConstantBuffer(f, firstSlot + 2 * drawCount + kReservedTailSlots)) return;

    ID3D12GraphicsCommandList* cmdList = commandList.Native();
    MeshConstants base = BindInstancedMultiPart(cmdList, constants, f, partsPerInstance);
    base.iblFlags[3] = prevOffset;
    UINT cbIndex = firstSlot + drawCount;
    RecordDepthOnlyDraws(commandList, base, f, DepthOnlyPass::Velocity, true, instanceCount, cbIndex);
}

} // namespace rs::render

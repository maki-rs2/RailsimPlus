// MeshPipelineRecordPrepass.cpp - MeshPipeline の深度専用の描画(影パスと主視点の深度プリパスで共通)
// 影パスもプリパスも、submesh を「ピクセルシェーダなし(Solid)」と「アルファ判定あり(AlphaTest)」の2組に
// 振り分けて深度だけ書く。振り分けの条件と PSO の組だけが違うので、描画の記録はここ1か所に持つ。
#include "MeshPipeline.h"

#include "SubmeshAlphaClassify.h"
#include "../shadow/CascadeFit.h"

namespace rs::render {

ShadowCasterKind MeshPipeline::DepthOnlyKindOf(const SubMesh& submesh, DepthOnlyPass pass,
                                               bool includeThin) const {
    if (pass != DepthOnlyPass::Shadow) return ClassifyPrepass(submesh);
    const ShadowCasterKind kind = ClassifyShadowCaster(submesh);
    if (kind == ShadowCasterKind::None || !MaterialCastsShadow(submesh)) return ShadowCasterKind::None;
    if (!includeThin && submesh.thinShadow) return ShadowCasterKind::None;
    return kind;
}

void MeshPipeline::RecordDepthOnlyDraws(const rs::rhi::CommandList& commandList, MeshConstants& base, UINT f,
                                        DepthOnlyPass pass, bool includeThin, UINT instanceCount,
                                        UINT& cbIndex) {
    ID3D12GraphicsCommandList* cmdList = commandList.Native();
    const bool shadow = pass == DepthOnlyPass::Shadow;
    // 使う PSO の組。影は影用(MeshPipelineSharedState の ShadowSolid/ShadowAlphaTest)、プリパスはプリパス用。
    const auto setPso = [&](bool alphaTest) {
        if (shadow) cmdList->SetPipelineState(alphaTest ? shared_->ShadowAlphaTest() : shared_->ShadowSolid());
        else if (pass == DepthOnlyPass::Velocity) shared_->BindVelocityPso(commandList, alphaTest);
        else shared_->BindPrepassPso(commandList, alphaTest);
    };
    // 深度だけなので描く順序は結果に影響しない。PSO の切り替えを1回ずつにするため、
    // 穴なし(Solid)→穴あき(AlphaTest)の順にまとめる。Solid は頂点シェーダだけでマテリアルを
    // 使わないため、インデックスが隣り合う submesh は1回の描画にまとめる。
    setPso(false);
    SubMesh run;
    bool hasRun = false;
    for (const SubMesh& submesh : geometry_.Submeshes()) {
        if (DepthOnlyKindOf(submesh, pass, includeThin) != ShadowCasterKind::Solid) continue;
        if (hasRun && run.indexOffset + run.indexCount == submesh.indexOffset) {
            run.indexCount += submesh.indexCount;
            continue;
        }
        if (hasRun) DrawInstancedSubmesh(cmdList, base, f, run, instanceCount, cbIndex, shadowConstantBuffer_);
        run = submesh;
        run.alphaTestClip = false;
        hasRun = true;
    }
    if (hasRun) DrawInstancedSubmesh(cmdList, base, f, run, instanceCount, cbIndex, shadowConstantBuffer_);

    bool alphaTestPsoSet = false;
    for (const SubMesh& submesh : geometry_.Submeshes()) {
        if (DepthOnlyKindOf(submesh, pass, includeThin) != ShadowCasterKind::AlphaTest) continue;
        if (!alphaTestPsoSet) {
            setPso(true);
            alphaTestPsoSet = true;
        }
        DrawInstancedSubmesh(cmdList, base, f, submesh, instanceCount, cbIndex, shadowConstantBuffer_);
    }
}

void MeshPipeline::RecordPrepassInstancedMultiPart(const rs::rhi::CommandList& commandList,
                                                   const MeshConstants& constants, UINT frameIndex,
                                                   UINT instanceCount, UINT partsPerInstance) {
    if (geometry_.IndexCount() == 0 || instanceCount == 0 || partsPerInstance == 0) return;
    const UINT drawCount = CountPrepassDraws(geometry_.Submeshes());
    if (drawCount == 0) return;
    const UINT f = frameIndex % kFrames;
    // 影パスと同じ定数バッファを使うが、スロットは影のカスケードぶんの後ろから取る。本編の
    // 定数バッファは同じフレームの後続の描画が書き換えるので、共有するとプリパスが後から書かれた値を読む。
    // 速度パスのスロット(プリパスの後ろ。RecordVelocityInstancedMultiPart)も、ここで一緒に確保する。
    const UINT firstSlot = CountShadowCasters(geometry_.Submeshes()) * static_cast<UINT>(kMaxShadowCascades);
    if (!EnsureShadowConstantBuffer(f, firstSlot + 2 * drawCount + kReservedTailSlots)) return;

    ID3D12GraphicsCommandList* cmdList = commandList.Native();
    MeshConstants base = BindInstancedMultiPart(cmdList, constants, f, partsPerInstance);
    UINT cbIndex = firstSlot;
    RecordDepthOnlyDraws(commandList, base, f, DepthOnlyPass::MainPrepass, true, instanceCount, cbIndex);
}

} // namespace rs::render

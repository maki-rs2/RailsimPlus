// MeshPipelineRecordShadow.cpp - MeshPipeline のシャドウパスの描画コマンド記録
// 通常の描画(MeshPipelineRecord.cpp)と同じ BindInstancedMultiPart・DrawInstancedSubmesh を使い、
// PSO と定数バッファだけを替える。
#include "MeshPipeline.h"

#include "SubmeshAlphaClassify.h"
#include "../shadow/CascadeFit.h"

namespace rs::render {

bool MeshPipeline::EnsureShadowConstantBuffer(UINT f, UINT requiredSlots) {
    if (shadowConstantBuffer_.Stride() == 0 && !shadowConstantBuffer_.Init(device_)) return false;
    return shadowConstantBuffer_.EnsureCapacity(device_, requiredSlots, f);
}

bool MeshPipeline::MaterialCastsShadow(const SubMesh& submesh) const {
    // submesh.castShadow は読込時の指定だけで決まる。条件つきの指定は実行時に材質へ書かれるので、
    // 影パスでは材質の現在の値を見る。
    const Material* material = materialSet_.MaterialAt(submesh.materialIndex);
    return !material || material->castShadow;
}

void MeshPipeline::RecordShadowInstancedMultiPart(const rs::rhi::CommandList& commandList,
                                                  const MeshConstants& lightConstants,
                                                  UINT frameIndex, UINT shadowSlice, UINT instanceCount,
                                                  UINT partsPerInstance,
                                                  bool includeThin) {
    if (geometry_.IndexCount() == 0 || instanceCount == 0 || partsPerInstance == 0) return;
    const UINT casterCount = CountShadowCasters(geometry_.Submeshes());
    if (casterCount == 0) return;
    const UINT f = frameIndex % kFrames;
    // カスケード1枚ぶんが casterCount スロット。最大枚数ぶんをまとめて確保し、枚ごとに先頭をずらす。
    if (shadowSlice >= kMaxShadowCascades) return;
    if (!EnsureShadowConstantBuffer(f, casterCount * static_cast<UINT>(kMaxShadowCascades) + kReservedTailSlots)) return;

    ID3D12GraphicsCommandList* cmdList = commandList.Native();
    MeshConstants base = BindInstancedMultiPart(cmdList, lightConstants, f, partsPerInstance);
    UINT cbIndex = shadowSlice * casterCount;
    RecordDepthOnlyDraws(commandList, base, f, DepthOnlyPass::Shadow, includeThin, instanceCount, cbIndex);
}

} // namespace rs::render

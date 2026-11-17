// MeshPipelineConstantsWrite.cpp - MeshPipeline のメッシュ定数バッファ書き込み
// (WriteMeshConstants/WriteMeshConstantsTo)。描画コマンド記録は MeshPipelineRecord.cpp。
#include "MeshPipeline.h"
#include "SubmeshAlphaClassify.h"
#include "../resource/RenderTuning.h"

#include <cstring>

namespace rs::render {

D3D12_GPU_VIRTUAL_ADDRESS MeshPipeline::WriteMeshConstants(
    const MeshConstants& base, UINT f, UINT cbSlot, uint32_t materialIndex,
    bool alphaTest) {
    return WriteMeshConstantsTo(constantBuffer_, base, f, cbSlot, materialIndex, alphaTest);
}

uint64_t MeshPipeline::WriteMeshConstantsTo(MeshConstantBuffer& target, const MeshConstants& base,
                                            UINT f, UINT cbSlot, uint32_t materialIndex,
                                            bool alphaTest, AlphaSplit alphaSplit) {
    MeshConstants c = base;
    if (c.material[3] == 0.0f) c.material[3] = 1.0f;
    c.materialIndex = materialIndex;
    // gInstanceMatOverridesのper-instanceストライドはDynamicMaterialSlotCount()
    // (実際の動的マテリアル数D)であり、DynamicMaterialSlots().size()(マテリアル
    // 番号→スロットのルックアップテーブルサイズM、通常D<M)ではない。取り違えると
    // instanceId>=1の全読み出しがずれる(詳細はMeshDynamicBuffers.h参照)。
    c.dynamicSlotCount = dynamicBuffers_.DynamicMaterialSlotCount();
    const GpuTexture* gt = materialSet_.TextureAt(materialIndex);
    // uvTransform は base 側が未初期化(呼び出し側は色/カメラ等のみ設定)なため、
    // materialIndex 範囲外でも必ず恒等へ明示リセットしておく。
    c.uvTransform0[0] = 1; c.uvTransform0[1] = 0;
    c.uvTransform0[2] = 0; c.uvTransform0[3] = 1;
    c.uvTransform1[0] = 0; c.uvTransform1[1] = 0;
    c.uvTransform1[2] = 0; c.uvTransform1[3] = 0;
    c.material[0] = 0.02f; // カットアウト閾値の既定(下で材質があれば上書き)
    c.material[1] = 0.0f;  // EnvMap指定の材質か(下で材質があれば上書き)
    if (const Material* material = materialSet_.MaterialAt(materialIndex)) {
        const Material& mat = *material;
        // RailSim II の alpha test は ref=0/GREATER =「alpha が完全に0の texel だけ
        // 破棄」。cutout は全てこれに合わせ、閾値を常にごく低く保つ。こうすると
        // 中間 alpha を持つ不透明テクスチャ(車体前面や屋根の陰影デカール等、
        // texel の1〜3割が中間 alpha)を cutout 描画しても穴が開かず、抜きテクス
        // チャの alpha=0 部分だけが正しく破棄される(閾値を高くすると、
        // こうした陰影デカールの中間 alpha が斑に破棄され穴が開く)。
        c.material[0] = 0.02f;
        ApplyMaterialColorToMeshConstants(mat, c);
        c.material[1] = mat.useEnvMap ? 1.0f : 0.0f;
        // z=テクスチャをフィルタ・ミップなしで読むか(インスタンス別の上書きでは変わらない)。
        c.uvTransform1[2] = AlphaZeroUsesPointSampling(mat.forceAlphaTest, AlphaZeroKBuildEnabled()) ? 1.0f : 0.0f;
    }
    bool hasTex = gt && gt->valid;
    c.useTexture[0] = hasTex ? 1.0f : 0.0f;
    c.useTexture[1] = (hasTex && gt->hasNormal) ? 1.0f : 0.0f;
    c.useTexture[2] = (hasTex && gt->hasOrm) ? 1.0f : 0.0f;
    const Material* shadowMaterial = materialSet_.MaterialAt(materialIndex);
    c.useTexture[3] = (shadowMaterial && !shadowMaterial->receiveShadow) ? 1.0f : 0.0f;
    c.material[2] = alphaTest ? 1.0f : 0.0f;
    // 穴あきの不透明側は上限未満のtexelを捨て、半透明側(z=2)は下限以上、上限未満だけ残す。
    // 深度専用パスも x を上限として読む。
    if (alphaSplit != AlphaSplit::None) {
        c.material[0] = RenderTuning::Embedded().semiAlphaHigh;
        c.material[2] = alphaSplit == AlphaSplit::SemiPart ? 2.0f : 1.0f;
    }
    // bindless テーブルの配列が直接参照する絶対添字。テクスチャ
    // 無し(gt==nullptrまたはvalid=false)は常にダミー(texSlotOffset=0)を指す。
    c.texIndex[0] = texturePool_.AbsoluteTexIndex(hasTex ? gt->texSlotOffset : 0);
    c.texIndex[1] = c.texIndex[2] = c.texIndex[3] = 0;

    uint8_t* dst = target.Mapped(f) + size_t(cbSlot) * target.Stride();
    std::memcpy(dst, &c, sizeof(MeshConstants));
    return target.GpuAddress(f, cbSlot);
}

} // namespace rs::render

// MeshMaterialRuntime.cpp - MeshPipeline の実行時マテリアル更新
// (UpdateMaterialColor/UpdateMaterialUvTransform/RevertMaterialTexture/
// UpdateMaterialTexture)。ChangeMaterial/ChangeTexture/SetAnimation等の
// カスタマイザが毎フレーム呼ぶ書き換えAPI。初回ロード(MeshUpload.cpp)、
// テクスチャプール管理(MeshTexturePool.cpp)から分離。
#include "MeshPipeline.h"

namespace rs::render {

void MeshPipeline::UpdateMaterialColor(uint32_t materialIndex, const Material& src) {
    Material* m = materialSet_.MutableMaterialAt(materialIndex);
    if (!m) return;
    m->diffuse[0] = src.diffuse[0];
    m->diffuse[1] = src.diffuse[1];
    m->diffuse[2] = src.diffuse[2];
    m->diffuse[3] = src.diffuse[3];
    m->specular[0] = src.specular[0];
    m->specular[1] = src.specular[1];
    m->specular[2] = src.specular[2];
    m->power = src.power;
    m->emissive[0] = src.emissive[0];
    m->emissive[1] = src.emissive[1];
    m->emissive[2] = src.emissive[2];
    // EnvMap / BumpMap の指定は条件ごとに実行時に変わる(不透明パスは useEnvMap を毎フレーム読む)。
    m->useEnvMap = src.useEnvMap;
    m->useBumpMap = src.useBumpMap;
    // AlphaZeroTest / NoCastShadow / NoReceiveShadow / Transparent の指定も条件ごとに実行時に変わる。
    // forceAlphaTest(点サンプリングの印)と receiveShadow は描画のたびに、castShadow は影パスで読む。
    m->forceAlphaTest = src.forceAlphaTest;
    m->castShadow = src.castShadow;
    m->receiveShadow = src.receiveShadow;

    // 半透明パス用 MatData: CPU 配列のみ書き換える。GPU 側バッファへの反映は
    // Record() がフレームごとに memcpy で行う（複数フレーム in flight の GPU
    // 読み出しと CPU 書き込みが同一バッファで競合しないようにするため）。
    if (materialIndex < transparency_.MutableMatDataCpu().size()) {
        ApplyMaterialColorToMatData(*m, transparency_.MutableMatDataCpu()[materialIndex]);
    }
}

void MeshPipeline::UpdateMaterialUvTransform(uint32_t materialIndex, const float uv6[6]) {
    Material* m = materialSet_.MutableMaterialAt(materialIndex);
    if (!m) return;
    for (int i = 0; i < 6; ++i) m->uvTransform[i] = uv6[i];

    if (materialIndex < transparency_.MutableMatDataCpu().size()) {
        MatData& d = transparency_.MutableMatDataCpu()[materialIndex];
        d.uvTransform0[0] = uv6[0]; d.uvTransform0[1] = uv6[1];
        d.uvTransform0[2] = uv6[2]; d.uvTransform0[3] = uv6[3];
        d.uvTransform1[0] = uv6[4]; d.uvTransform1[1] = uv6[5];
    }
}

void MeshPipeline::RevertMaterialTexture(uint32_t materialIndex) {
    const GpuTexture* base = materialSet_.BaseTextureAt(materialIndex);
    if (!base) return;
    UpdateMaterialTexture(materialIndex, base->valid, base->texSlotOffset, base->hasNormal,
                         base->hasOrm);
}

void MeshPipeline::RebindMaterialBaseTexture(uint32_t materialIndex, bool hasTexture,
                                             uint32_t texSlotOffset, bool hasNormal,
                                             bool hasOrm) {
    materialSet_.SetBaseTextureAssignment(materialIndex, hasTexture, texSlotOffset, hasNormal,
                                          hasOrm);
    UpdateMaterialTexture(materialIndex, hasTexture, texSlotOffset, hasNormal, hasOrm);
}

void MeshPipeline::UpdateMaterialTexture(uint32_t materialIndex, bool hasTexture,
                                         uint32_t texSlotOffset, bool hasNormal,
                                         bool hasOrm) {
    GpuTexture* gt = materialSet_.MutableTextureAt(materialIndex);
    if (!gt) return;
    gt->valid = hasTexture;
    gt->hasNormal = hasTexture && hasNormal;
    gt->hasOrm = hasTexture && hasOrm;
    gt->texSlotOffset = hasTexture ? texSlotOffset : 0;

    if (materialIndex < transparency_.MutableMatDataCpu().size()) {
        MatData& d = transparency_.MutableMatDataCpu()[materialIndex];
        d.flags[0] = gt->hasNormal ? 1.0f : 0.0f;
        d.flags[1] = gt->hasOrm ? 1.0f : 0.0f;
        d.tex[0] = texturePool_.AbsoluteTexIndex(gt->texSlotOffset);
        d.tex[1] = gt->valid ? 1u : 0u;
    }
}

} // namespace rs::render

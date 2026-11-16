//
// MeshMaterialSet.h - MeshPipeline のマテリアル/テクスチャ割当ストレージ
//
// マテリアル配列(materials_)、materialIndex ごとの現在テクスチャ割当
// (textures_)、UploadMesh() 直後のスナップショット(baseTextures_、
// RevertMaterialTexture 用)をまとめて管理する。
//
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "MeshPipelineTypes.h"

namespace rs::render {

class MeshMaterialSet {
public:
    void Assign(std::vector<Material> materials);
    void SnapshotBase();
    void Clear();

    std::vector<Material>& MutableMaterials() { return materials_; }
    const std::vector<Material>& Materials() const { return materials_; }
    size_t Count() const { return materials_.size(); }

    std::vector<GpuTexture>& MutableTextures() { return textures_; }
    const std::vector<GpuTexture>& Textures() const { return textures_; }
    const GpuTexture* TextureAt(uint32_t materialIndex) const {
        return materialIndex < textures_.size() ? &textures_[materialIndex] : nullptr;
    }
    GpuTexture* MutableTextureAt(uint32_t materialIndex) {
        return materialIndex < textures_.size() ? &textures_[materialIndex] : nullptr;
    }
    const Material* MaterialAt(uint32_t materialIndex) const {
        return materialIndex < materials_.size() ? &materials_[materialIndex] : nullptr;
    }
    Material* MutableMaterialAt(uint32_t materialIndex) {
        return materialIndex < materials_.size() ? &materials_[materialIndex] : nullptr;
    }
    const GpuTexture* BaseTextureAt(uint32_t materialIndex) const {
        return materialIndex < baseTextures_.size() ? &baseTextures_[materialIndex] : nullptr;
    }
    // UploadMesh 直後のスナップショット(RevertMaterialTexture の戻り先)のうち、
    // 1マテリアルのテクスチャの割当だけを書き換える。
    void SetBaseTextureAssignment(uint32_t materialIndex, bool valid, uint32_t texSlotOffset,
                                  bool hasNormal, bool hasOrm) {
        if (materialIndex >= baseTextures_.size()) return;
        GpuTexture& base = baseTextures_[materialIndex];
        base.valid = valid;
        base.hasNormal = valid && hasNormal;
        base.hasOrm = valid && hasOrm;
        base.texSlotOffset = valid ? texSlotOffset : 0;
    }

private:
    std::vector<Material> materials_;
    std::vector<GpuTexture> textures_;
    std::vector<GpuTexture> baseTextures_;
};

} // namespace rs::render

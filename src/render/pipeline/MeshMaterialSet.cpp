// MeshMaterialSet.cpp - MeshPipeline のマテリアル/テクスチャ割当ストレージ
#include "MeshMaterialSet.h"

#include <utility>

namespace rs::render {

void MeshMaterialSet::Assign(std::vector<Material> materials) {
    materials_ = std::move(materials);
    textures_.assign(materials_.size(), GpuTexture{});
}

void MeshMaterialSet::SnapshotBase() {
    baseTextures_ = textures_;
}

void MeshMaterialSet::Clear() {
    materials_.clear();
    textures_.clear();
    baseTextures_.clear();
}

} // namespace rs::render

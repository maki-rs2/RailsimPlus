//
// ModelManager.cpp
//
#include "ModelManager.h"

namespace rs::render {

size_t ModelManager::ApproxBytes(const rs::legacy_x::LoadResult& r) {
    // 頂点/インデックスの実バイト数で概算(マテリアル/ログは小さいので無視)。
    return r.mesh.vertices.size() * sizeof(rs::render::MeshVertex) +
           r.mesh.indices.size() * sizeof(uint32_t);
}

ModelManager& GetModelManager() {
    static ModelManager sManager;
    return sManager;
}

} // namespace rs::render

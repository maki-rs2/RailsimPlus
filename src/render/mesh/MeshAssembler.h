#pragma once

#include <cstddef>
#include <vector>

#include "Mesh.h"

namespace rs::render {

class MeshAssembler {
public:
    // thinShadow が true なら、積んだ submesh に SubMesh::thinShadow を付ける(同じ材質でも別の束にする)。
    void Append(const Mesh& mesh, bool thinShadow = false);
    Mesh Build() const;

    size_t SourceSubmeshCount() const { return sourceSubmeshCount_; }

private:
    struct MaterialBucket {
        Material material;
        bool thinShadow = false;
        bool vertexAlpha = false; // 頂点色のアルファが1未満の submesh は、同じ材質でも別の束にする
        std::vector<uint32_t> indices;
    };

    size_t FindOrAddMaterial(const Material& material, bool thinShadow, bool vertexAlpha);

    std::vector<MeshVertex> vertices_;
    std::vector<MaterialBucket> materialBuckets_;
    size_t sourceSubmeshCount_ = 0;
};

} // namespace rs::render

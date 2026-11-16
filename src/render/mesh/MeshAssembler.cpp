#include "MeshAssembler.h"

#include <limits>
#include <utility>

#include <spdlog/spdlog.h>

#include "MaterialCompare.h"

namespace rs::render {

size_t MeshAssembler::FindOrAddMaterial(const Material& material, bool thinShadow, bool vertexAlpha) {
    for (size_t i = 0; i < materialBuckets_.size(); ++i) {
        if (materialBuckets_[i].thinShadow == thinShadow && materialBuckets_[i].vertexAlpha == vertexAlpha &&
            MaterialsEqualExceptAmbientAndName(materialBuckets_[i].material, material))
            return i;
    }
    MaterialBucket bucket;
    bucket.material = material;
    bucket.thinShadow = thinShadow;
    bucket.vertexAlpha = vertexAlpha;
    materialBuckets_.push_back(std::move(bucket));
    return materialBuckets_.size() - 1;
}

void MeshAssembler::Append(const Mesh& mesh, bool thinShadow) {
    constexpr size_t kMaxUint32 = std::numeric_limits<uint32_t>::max();
    if (vertices_.size() > kMaxUint32 ||
        mesh.vertices.size() > kMaxUint32 - vertices_.size()) {
        spdlog::warn("[mesh] assembler skipped mesh because combined vertex count exceeds uint32 range");
        return;
    }
    const uint32_t vertexBase = static_cast<uint32_t>(vertices_.size());
    for (const MeshVertex& sourceVertex : mesh.vertices) {
        MeshVertex vertex = sourceVertex;
        // このassemblerの入力は全てCPU側でワールド座標へ焼き込み済みの静的配置。
        // 元モデルのpartIndexを残すと、主メッシュのgPartWorld[0]だけを使う経路で
        // 範囲外SRVアクセスになるため、静的パーツとして固定する。
        vertex.partIndex = 0;
        vertices_.push_back(vertex);
    }

    for (const SubMesh& submesh : mesh.submeshes) {
        ++sourceSubmeshCount_;
        if (submesh.materialIndex >= mesh.materials.size()) {
            spdlog::warn("[mesh] assembler skipped submesh with invalid material index {} (materials={})",
                         submesh.materialIndex, mesh.materials.size());
            continue;
        }
        if (submesh.indexOffset > mesh.indices.size() ||
            submesh.indexCount > mesh.indices.size() - submesh.indexOffset) {
            spdlog::warn("[mesh] assembler skipped submesh with invalid index range offset={} count={} (indices={})",
                         submesh.indexOffset, submesh.indexCount, mesh.indices.size());
            continue;
        }
        bool hasInvalidVertexIndex = false;
        for (uint32_t index = 0; index < submesh.indexCount; ++index) {
            if (mesh.indices[submesh.indexOffset + index] >= mesh.vertices.size()) {
                hasInvalidVertexIndex = true;
                break;
            }
        }
        if (hasInvalidVertexIndex) {
            spdlog::warn("[mesh] assembler skipped submesh with vertex index outside vertex range (vertices={})",
                         mesh.vertices.size());
            continue;
        }

        const size_t materialBucket = FindOrAddMaterial(mesh.materials[submesh.materialIndex], thinShadow,
                                                    submesh.vertexAlpha);
        std::vector<uint32_t>& indices = materialBuckets_[materialBucket].indices;
        for (uint32_t index = 0; index < submesh.indexCount; ++index) {
            indices.push_back(mesh.indices[submesh.indexOffset + index] + vertexBase);
        }
    }
}

Mesh MeshAssembler::Build() const {
    Mesh mesh;
    mesh.vertices = vertices_;
    for (const MaterialBucket& bucket : materialBuckets_) {
        if (bucket.indices.empty()) continue;
        SubMesh submesh;
        submesh.materialIndex = static_cast<uint32_t>(mesh.materials.size());
        submesh.indexOffset = static_cast<uint32_t>(mesh.indices.size());
        submesh.indexCount = static_cast<uint32_t>(bucket.indices.size());
        submesh.thinShadow = bucket.thinShadow;
        submesh.vertexAlpha = bucket.vertexAlpha;
        mesh.materials.push_back(bucket.material);
        mesh.indices.insert(mesh.indices.end(), bucket.indices.begin(), bucket.indices.end());
        mesh.submeshes.push_back(submesh);
    }
    mesh.ComputeAabb();
    return mesh;
}

} // namespace rs::render

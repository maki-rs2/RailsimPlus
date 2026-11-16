//
// StaticMeshVariantSet.cpp
//
#include "StaticMeshVariantSet.h"

#include <algorithm>
#include <cstring>

namespace rs::app {

size_t BandIndexOfStart(const std::vector<int32_t>& bandStarts, int32_t bandStart) {
    if (bandStarts.empty()) return 0;
    // 開始値がbandStart以下の最後のバンド。
    std::vector<int32_t>::const_iterator it =
        std::upper_bound(bandStarts.begin(), bandStarts.end(), bandStart);
    return it == bandStarts.begin() ? 0 : static_cast<size_t>(it - bandStarts.begin()) - 1;
}

bool StaticMeshVariantSet::HasAnyHeadlight() const {
    for (const std::shared_ptr<const std::vector<StaticHeadlight>>& lights : headlightsOfVariant) {
        if (lights != nullptr) return true;
    }
    return false;
}

size_t StaticMeshVariantSet::VariantIndex(size_t stateSlot, int32_t bandStart) const {
    const size_t band = BandIndexOfStart(bandStarts, bandStart);
    const size_t index = stateSlot * BandCount() + band;
    return index < variantOfBand.size() ? variantOfBand[index] : 0;
}

namespace {

bool BitsEqual(const float* a, const float* b, size_t count) {
    return std::memcmp(a, b, sizeof(float) * count) == 0;
}

bool MaterialsIdentical(const rs::render::Material& a, const rs::render::Material& b) {
    // Material::operator==(全フィールド)に加え、浮動小数はビット列でも一致させる。
    return a == b && BitsEqual(a.diffuse, b.diffuse, 4) && BitsEqual(a.ambient, b.ambient, 3) &&
           BitsEqual(a.specular, b.specular, 3) && BitsEqual(a.emissive, b.emissive, 3) &&
           BitsEqual(&a.power, &b.power, 1) && BitsEqual(a.uvTransform, b.uvTransform, 6);
}

bool VerticesIdentical(const rs::render::MeshVertex& a, const rs::render::MeshVertex& b) {
    return BitsEqual(a.pos, b.pos, 3) && BitsEqual(a.normal, b.normal, 3) &&
           BitsEqual(a.uv, b.uv, 2) && a.partIndex == b.partIndex &&
           std::equal(std::begin(a.color), std::end(a.color), std::begin(b.color));
}

bool SubMeshesIdentical(const rs::render::SubMesh& a, const rs::render::SubMesh& b) {
    return a.materialIndex == b.materialIndex && a.indexOffset == b.indexOffset &&
           a.indexCount == b.indexCount && a.alphaClass == b.alphaClass &&
           a.alphaTestClip == b.alphaTestClip && a.alphaSplit == b.alphaSplit &&
           a.vertexAlpha == b.vertexAlpha && BitsEqual(a.center, b.center, 3);
}

} // namespace

bool MeshesIdentical(const rs::render::Mesh& a, const rs::render::Mesh& b) {
    return a.vertices.size() == b.vertices.size() && a.indices.size() == b.indices.size() &&
           a.submeshes.size() == b.submeshes.size() && a.materials.size() == b.materials.size() &&
           a.indices == b.indices &&
           std::equal(a.vertices.begin(), a.vertices.end(), b.vertices.begin(), VerticesIdentical) &&
           std::equal(a.submeshes.begin(), a.submeshes.end(), b.submeshes.begin(),
                      SubMeshesIdentical) &&
           std::equal(a.materials.begin(), a.materials.end(), b.materials.begin(),
                      MaterialsIdentical);
}

} // namespace rs::app

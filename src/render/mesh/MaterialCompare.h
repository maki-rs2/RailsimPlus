//
// MaterialCompare.h - Material の比較
//
#pragma once

#include <algorithm>
#include <iterator>

#include "Mesh.h"

namespace rs::render {

// texturePath/normalPath/ormPath 以外のフィールドがすべて等しいか。
// スイッチだけでテクスチャが変わったマテリアルかどうかを見分けるのに使う。
// 比較は Material::operator==(全フィールド、defaultなので新フィールドも自動で含まれる)に
// 一本化し、テクスチャパスだけを揃えたコピーと比べる。
inline bool MaterialsEqualExceptTexturePaths(const Material& a, const Material& b) {
    Material aligned = a;
    aligned.texturePath = b.texturePath;
    aligned.normalPath = b.normalPath;
    aligned.ormPath = b.ormPath;
    return aligned == b;
}

// ambient と name 以外のフィールドがすべて等しいか。MeshAssembler が、同じ見た目の
// マテリアルを1つに束ねるかの判定に使う。
// MaterialsEqualExceptTexturePaths と同じく、Material::operator== を基にするので、
// フィールドが増えても比べ漏れない。
inline bool MaterialsEqualExceptAmbientAndName(const Material& a, const Material& b) {
    Material aligned = a;
    std::copy(std::begin(b.ambient), std::end(b.ambient), std::begin(aligned.ambient));
    aligned.name = b.name;
    return aligned == b;
}

} // namespace rs::render

//TrainPickBox.h - 編成車両の赤枠とピッキングに使う箱を、描画と同じパーツ行列から作る
#pragma once

#include <cstddef>
#include <vector>

#include "../../core/Math.h"
#include "../../render/mesh/Mesh.h"

namespace rs::app {

//パーツ1つぶんのローカルAABB。頂点が無いパーツはvalid=false
struct PartLocalBounds {
    float min[3] = {0.0f, 0.0f, 0.0f};
    float max[3] = {0.0f, 0.0f, 0.0f};
    bool valid = false;
};

//合成メッシュの頂点をpartIndexで振り分けて、パーツごとのローカルAABBを求める
//partCount以上のpartIndexを持つ頂点は無視する
std::vector<PartLocalBounds> ComputePartLocalBounds(
    const std::vector<rs::render::MeshVertex>& vertices, size_t partCount);

//基準フレームのローカル座標で見た箱
struct TrainPickBox {
    bool valid = false;
    float aabbMin[3] = {0.0f, 0.0f, 0.0f};
    float aabbMax[3] = {0.0f, 0.0f, 0.0f};
};

//included[i]がtrueのパーツだけを対象に、partWorlds[i]でワールドへ置いた8頂点を
//frameのローカルへ戻して包む。frameは剛体(回転と平行移動だけ)であること
//パーツの姿勢は描画と同じ行列を使うので、曲線でも車体の向きに箱が沿う
TrainPickBox BuildTrainPickBox(const std::vector<PartLocalBounds>& bounds,
                               const std::vector<rs::core::Mat4>& partWorlds,
                               const std::vector<bool>& included, const rs::core::Mat4& frame);

} // namespace rs::app

//
// NodeDragCenterline.h - 複線中心線ノードのドラッグで、メンバー(実レール)のノードを追従させる計画
//
// 中心線ノード C を動かすと、各メンバー(切り離し済みを除く)の「近い側」ノードを、新しい C の
// 姿勢だけから決める(水平曲線を辿った結果は使わない)。
//   位置 = C.新位置 + right(C.新接線の水平) × 横オフセット
//   高さ = C.新位置.y + (メンバー.y - C.旧位置.y)   (同じΔy)
//   接線 = C の新しい接線(水平方向とピッチ)
// 横オフセットは都度 dot(メンバー位置 - C.旧位置, right(C.旧接線の水平)) で求め、保存しない。
// メンバーの遠い側ノードは動かさない。形は NodeDragPlan が BuildEdgeShape で確認する。
//
#pragma once

#include <cmath>
#include <vector>

#include "../../core/Epsilon.h"
#include "../../world/alignment/TrackGraph.h"
#include "NodeDragPlan.h"

namespace rs::editor {

// 水平成分の単位ベクトル(ほぼ鉛直なら false)。
inline bool HorizontalUnit(const rs::core::Vec3& v, rs::core::Vec3& out) {
    const float len = std::hypot(v.x, v.z);
    if (len < rs::core::kDegenerateVectorEps) return false;
    out = rs::core::Vec3(v.x / len, 0.0f, v.z / len);
    return true;
}

// 中心線ノードをドラッグできるか: ノードの全edgeが中心線(centerlineOnly)で、各中心線が
// メンバーを2本以上持ち、切り離し済みメンバーの近い側ノードが他のedgeと共有されていない。
bool IsCenterlineNodeMovable(const rs::world::TrackGraph& graph,
                                 const rs::world::Guid& nodeGuid);

// ノードに中心線(centerlineOnly)edgeがつながっているか。
bool NodeHasCenterlineEdge(const rs::world::TrackGraph& graph, const rs::world::Guid& nodeGuid);

// 中心線ノード center が newPose になるときの、メンバー近い側ノードの変更を out へ追加する。
// 連続敷設のジャンクションで2本の中心線から同じメンバーノードに行き着いても1回だけ計画する。
// メンバーやそのノードが無ければ false(out は不定)。
bool PlanCenterlineMemberFollow(const rs::world::TrackGraph& graph, const rs::world::Guid& center,
                                const NodePoseChange& newPose, std::vector<NodePoseChange>& out);

} // namespace rs::editor

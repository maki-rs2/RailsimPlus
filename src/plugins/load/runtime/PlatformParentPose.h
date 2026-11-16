//
// PlatformParentPose.h - Platform の ParentObject の姿勢に追従する点の計算。
//
#pragma once

#include "../../../core/Math.h"

namespace rs::app {

// 親オブジェクトのワールド行列で、Coord(親のローカル座標)をワールドへ動かした点。
// 行列の3本の軸はスケールを除いて長さ1にそろえ、平行移動はそのまま使う
// (正規化は rs::world::NormalizedRigidPose と同じ)。計算は double、戻り値は GPU/グラフ向けの float。
rs::core::Vec3 TransformPlatformParentPoint(const rs::core::Mat4& parentWorld,
                                            const rs::core::Vec3& coord);

} // namespace rs::app

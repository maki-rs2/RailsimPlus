//
// ScenePlacement.h - シーン常駐エンティティの設置座標表現
//
// 軌道上配置(TrackPlacement)とワールド自由配置(FreePlacement)の2種類を持ち、
// ResolveWorldMatrix() でどちらもワールド行列(row-major, v'=v*M)へ解決する。
//
#pragma once

#include <variant>

#include "../../core/Math.h"
#include "../alignment/TrackTraversal.h"

namespace rs::world {

// 反転した車両は、軌道の向きに対して水平に半回転させる(個体の向きを軌道へ合わせ直さない)。
inline constexpr float kReversedCarYawDeg = 180.0f;
inline constexpr float CarYawOffsetDeg(bool reversed) { return reversed ? kReversedCarYawDeg : 0.0f; }

// 軌道上の設置(レール系の離散配置=Tie/Pier/Pole、編成の各車両)。
// yawOffsetDeg/lateralOffsetM/verticalOffsetMは軌道接線基準からのローカルな追加調整
// (編成の反転車両はyawOffsetDeg=180を設定する)。
// verticalOffsetMはup方向(frame.up)への追加オフセット[m]で、Pole(架線柱)の
// LineInfo.TrolleyAlt+Height(軌道面からモデル原点=架線支持点までの高さ)等に使う。
// keepUpright=trueの場合、配置位置(lateral/verticalOffsetM)はカント適用済み
// frame.upを基準に計算するがモデル自体の姿勢はワールドup基準(カントによる
// ロール回転なし)にする。Pole(架線柱)は現実の柱がカントで傾かないため使う。
struct TrackPlacement {
    TrackPosition position;
    float yawOffsetDeg = 0.0f;
    float lateralOffsetM = 0.0f;
    float verticalOffsetM = 0.0f;
    bool keepUpright = false;
};

// ワールド自由座標での設置(情景プラグイン)。汎用3軸回転に対応する。
// 合成順序は内因性(intrinsic) yaw→pitch→roll(航空機規約)で固定。
// yaw: ワールドup軸周り、pitch: yaw後のright軸周り、roll: yaw+pitch後のforward軸周り。
// ギズモ操作や.rs2シリアライズもこの順序に合わせること。
struct FreePlacement {
    rs::core::Vec3 position;
    float yawDeg = 0.0f;
    float pitchDeg = 0.0f;
    float rollDeg = 0.0f;
};

using ScenePlacement = std::variant<TrackPlacement, FreePlacement>;

// ScenePlacementをワールド行列へ解決する(計算はfloat。TrackFrame自体がfloat精度のため、
// double/float境界はTrackPosition::s側の弧長計算で満たされている)。
// TrackPlacementでedgeがgraphに存在しない場合は単位行列を返す。
rs::core::Mat4 ResolveWorldMatrix(const ScenePlacement& placement, const TrackGraph& graph);

// pos/dir/up(ワールド座標の直交フレーム、.rs2のPos/Dir/Up等)から FreePlacement の
// yaw/pitch/rollDeg を逆算する(ResolveWorldMatrixのFreePlacement合成の逆変換)。dir/upは正規化・直交化してから使うため
// 入力が厳密な直交でなくてもよい(Gram-Schmidt補正)。dirがワールドup(0,1,0)とほぼ平行な
// 縮退ケース(真上/真下向き)はyawを0とみなす。
FreePlacement DecomposeFreePlacement(const rs::core::Vec3& pos, const rs::core::Vec3& dir,
                                     const rs::core::Vec3& up);

} // namespace rs::world

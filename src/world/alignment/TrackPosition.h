//
// TrackPosition.h - 軌道上の位置・フレームの値型
//
// TrackPosition（edge上の位置）・TrackFrame（位置の座標系）・NodeSlotOverride を置く。
// 位置を持つだけのヘッダが、走査カーネル(TrackTraversal.h)と TrackNode.h を読まずに済むよう分けてある。
//
#pragma once

#include <array>
#include <optional>
#include <unordered_map>

#include "../../core/Math.h"
#include "Guid.h"

namespace rs::world {

// 分岐nodeの列車ごとの決定済み開通先。keyはTrackNode::guid、値は面(0/1)別の
// ordinal(未決定の面はstd::nullopt)。SelectNextRealLinkへ渡すと、共有
// activeSlotより優先してこの値を使う。
using NodeSlotOverride = std::unordered_map<Guid, std::array<std::optional<int>, 2>, GuidHash>;

// 軌道上の位置。edge.spline は nodeA 起点の弧長 s 昇順で保持されるため、
// (edge, s, direction) の3値で一意に位置と進行方向が定まる。
struct TrackPosition {
    Guid   edge = NilGuid();  // 現在乗っている edge
    double s    = 0.0;        // edge の nodeA 起点からの弧長[m]（0..edgeLength）
    int    direction  = +1;   // 進行方向: +1 = nodeA→nodeB, -1 = nodeB→nodeA
};

// EvaluateAt が返す軌道フレーム（位置・接線・アップ・カント）。
// spline 自体が float 精度のため pos/tangent/up は Vec3(float) のまま返す
// （double/float境界は s の弧長計算・補間比率側で満たす）。
struct TrackFrame {
    rs::core::Vec3 pos;
    rs::core::Vec3 tangent;  // 接線（正規化）。direction=-1 のときは反転済み
    rs::core::Vec3 up;   // アップベクトル（カント適用済み、正規化）
    float cant = 0.0f;   // カント(tan比、RailSim II の CRailConnector::m_Cant と同値。無次元)
};

} // namespace rs::world

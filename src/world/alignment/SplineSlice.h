//
// SplineSlice.h - edge.spline から弧長[s0,s1]の部分列を切り出すユーティリティ。
//
// edge内プラグイン区間の押し出しメッシュ生成
// (rs::legacy_plugin::BuildRailMeshAlongSpline)が、TrackEdge::pluginSections
// (IntervalList<PluginSet>)の各区間だけを対象に押し出すために使う。
//
#pragma once

#include <vector>

#include "TrackGraph.h"

namespace rs::world {

// edge.spline の [s0, s1] 部分列を切り出す。範囲内の既存サンプルはそのまま使い、
// 両端は EvaluateAt で補間したフレームを新規サンプルとして挿入する(境界がサンプル
// 点にスナップせず滑らかにつながる)。
//
// 高速パス: edge全長(edge.spline.back().s)に対して s0<=0.0 かつ s1>=total なら
// edge.spline をそのまま返す(大規模レイアウトで全edgeが毎回フル再サンプルに
// なることを避けるため必須)。
//
// s1<=s0、またはedge.spline.empty()の場合は空を返す。s0/s1はedge全長へclampする。
std::vector<TrackNodeSample> SliceSpline(const TrackEdge& edge, double s0, double s1);

}  // namespace rs::world

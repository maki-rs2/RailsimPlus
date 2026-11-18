//
// Rs2CurveFit.h - .rs2 の Splitter列(疎な生データ)から水平線形(直線/円弧)を復元する。
// .rs2形式は緩和曲線(クロソイド等)を持たず直線+純円弧のみ(RailSim II のエディタの敷設は
// 直線と純円弧だけで、緩和曲線は無い)であるため、
// 区間ごとの水平偏角から直線/円弧を判別し AlignmentSegment 列へ変換できる。
// この復元は水平線形のみを扱う(edge の curve/spline は両端ノードの3D姿勢から
// RebuildEdgeSpline が作り、ここの結果は生成に失敗したときのフォールバックにだけ使う)。
//
#pragma once

#include <vector>

#include "Rs2Types.h"
#include "../../world/alignment/AlignmentSegment.h"

namespace rs::world {
struct TrackEdge;
struct TrackNode;
} // namespace rs::world

namespace rs::legacy_layout {

// splitters(疎なSplitter列, nodeA→nodeB順)から水平線形の AlignmentSegment 列を
// 復元する。隣接区間の水平偏角が閾値未満なら直線、それ以外は同符号の区間をまとめて
// 円弧とし、弦長と偏角から半径を(区間長で重み付け平均して)復元する。
// RailSim II は緩和曲線を持たないため Kind::Transition は生成しない。
// splitters.size()<2、または全区間が重複点でchordLenを持てない場合は空を返す。
std::vector<rs::world::AlignmentSegment> FitCurveFromSplitters(
    const std::vector<Rs2Splitter>& splitters);

// splitters(疎なSplitter列, nodeA→nodeB順)から、隣接Splitterペアごとに1本の
// 直線/3D円弧を生成する密な水平線形。FitCurveFromSplittersのように複数区間を
// 1本へ圧縮しない(3D圧縮フィットは最悪13.5cm乖離するため使わない)。Y成分を含む3D弦・接線を
// そのまま使うため、勾配とカーブが同時に変化する区間でも入力Splitterへ追従する。
// splitters.size()<2の場合は空を返す。
std::vector<rs::world::AlignmentSegment> BuildCurveFromSplitters3D(
    const std::vector<Rs2Splitter>& splitters);

// edge.curve(FitCurveFromSplittersの復元結果)が、両端ノードの位置・接線だけから
// 単一の3点フィレット(頂点1つ・円弧1本+前後任意の直線、緩和曲線挿入も可)として
// 再現できるかを判定する。再現できるedgeのみ TrackEdge::Refit::valid を立て、
// 敷設エディタの「緩和曲線の後編集」対象にしてよい。
// 判定はTryRefitThreePointArc(SolveThreePointArc)の出力をSampleAlignmentでサンプルし、
// spline実測点列との水平位置の双方向Hausdorff距離が閾値以下かで見る(複数円弧から
// なるS字等の複合曲線が見当違いの単一円弧にフィットしても乖離で検知できる)。
// 注意: SolveThreePointArcは d=min(aLen,bLen) を等辺とした二等辺三角形フィレットで
// 解くため、前後直線の長さが非対称な「直線+円弧+直線」は値域外で必ずfalseになる
// (Rs2ImportTests.cpp「rejects a line-arc-line edge with straights on both sides」参照)。
// 純円弧・片側のみ直線を持つedgeはtrueになる。
// curve/splineはedgeの復元結果、nodeA/nodeBは対応するTrackNode(pos/dir/up)。
bool IsThreePointFilletRepresentable(const rs::world::TrackEdge& edge,
                                     const rs::world::TrackNode& nodeA,
                                     const rs::world::TrackNode& nodeB);

} // namespace rs::legacy_layout

//
// EdgeShapeGenerator.h - 両端の3D姿勢から edge の線形(curve)を作る生成器
//
// RailSim2 の CRailCurve::CalcSplit と同じ再帰分割で、2つの端点姿勢(位置+ピッチ込みの
// 接線)から Line / 3D Arc(axisTiltRad で傾いた平面の円弧)の列を作る。
// 取り込みedgeと敷設エディタ産edgeで生成器を分けないための単一の入口。
// 緩和曲線・複線オフセット・カントは含まない(後の段階で EdgeShapeOptions の拡張で足す)。
//
#pragma once

#include <string>
#include <vector>

#include "TrackGraph.h"

#include "../../core/Epsilon.h"

namespace rs::world {

// edge 端点の3D姿勢。dir は勾配(ピッチ)込みの3D単位ベクトル。
struct EdgeEndPose {
    rs::core::Vec3 pos;
    rs::core::Vec3 dir;
    rs::core::Vec3 up;
};

struct EdgeShapeOptions {
    // 葉の判定値[m]。|pos1+L*dir1-pos2| + |pos1+L*dir2-pos2| がこれ未満なら1本の円弧で足りる
    // (RailSim II と同じ0.2)。
    double leafToleranceM = 0.2;
    // 葉と判定した区間でも、左右対称の1本の円弧で結んだ終点と終点姿勢との距離がこれ以上なら、
    // 中点で分ける。終点の検証の許容(endPositionToleranceM)より小さくして、葉を重ねたときの
    // 誤差の積み上がりとfloat変換の誤差を吸収する。
    double leafClosureToleranceM = 0.0005;
    // 1区間の最大長[m]。これより長い区間は葉にせず分割する(RailSim II のRAIL_SEG_MAX)。
    double maxSegmentLengthM = 100.0;
    // 再帰分割の深さ上限と、区間数の上限。超えたら解けないとして ok=false にする。
    int maxDepth = 40;
    int maxSegmentCount = 20000;
    // 隣接2点の接線のなす角がこれ未満なら Line にする。終点の向きの許容(0.01°=1.7e-4rad)より
    // 十分小さくしないと、小さな偏角の区間が直線に潰れて終点がずれる。
    float lineAngleEpsRad = rs::core::kLineAngleEpsRad;
    // 生成した curve の終点が end に一致すべき許容。外れたら ok=false にする。
    double endPositionToleranceM = 0.002;  // 葉1本の円弧で結べない姿勢(接線と弦が同一平面にない)の残差を含める
    double endDirectionToleranceDeg = 0.01;
    // 緩和曲線つき(水平形を3点フィレットソルバで作る経路)の終点位置の許容。ソルバの移程量の
    // 数値積分と、サンプル時の積分の差で数mmの残差が出るため別枠にする。
    double transitionEndPositionToleranceM = 0.02;
};

struct EdgeShapeResult {
    bool ok = false;
    std::vector<AlignmentSegment> curve;  // 失敗時は空
    std::string error;                    // 失敗理由(成功時は空)
    // 検証で測った終点の誤差(失敗時の調査用。成功時も入る)。
    double endPositionErrorM = 0.0;
    double endDirectionErrorDeg = 0.0;
};

// start/end の姿勢を満たす curve を生成する。解けないとき(NaN・同一点・再帰上限・
// 終点が許容内で一致しない等)は ok=false で curve は空。例外は投げない。
EdgeShapeResult GenerateEdgeCurve(const EdgeEndPose& start, const EdgeEndPose& end,
                                  const EdgeShapeOptions& opt = {});

// 姿勢の列(隣接ペアごとに1区間)を Line / 3D Arc の列にする。
// 始点フレームは先頭の dir/up で、2点目以降の up は使わない(SampleAlignment が区間ごとに
// 引き継ぐため)。隣接2点の接線が平行なら Line、そうでなければ弦と両端接線から半径を
// 決めた Arc(axisTiltRad は回転軸の傾き)。重複点は読み飛ばす。
// lineAngleEpsRad: 隣接2点の接線のなす角(外積の長さ)がこれ未満なら Line にする。
// 既定値は取り込み経路(BuildCurveFromSplitters3D)が使う値。
constexpr float kDefaultLineAngleEpsRad = rs::core::kLineAngleEpsRad;
std::vector<AlignmentSegment> BuildCurveFromPoseChain(
    const std::vector<EdgeEndPose>& poses, float lineAngleEpsRad = kDefaultLineAngleEpsRad);

} // namespace rs::world

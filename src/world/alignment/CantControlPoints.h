//
// CantControlPoints.h - カント制御点列(TrackEdge::cantPoints)の補間・間引き・分割・連結
//
// 制御点は弧長→カント値の折れ線。.rs2取り込み・分割・結合が同じ関数で作り、
// CantProfile が同じ関数で読む。
//
#pragma once

#include <utility>
#include <vector>

#include "TrackGraph.h"

namespace rs::world {

// 間引きの許容(tan比)。軌間1067mmで約0.2mmのレール高さ差。
constexpr float kCantThinTolerance = 2.0e-4f;

// 弧長sのカント値。両端(0,cantStart)・(totalLen,cantEnd)を暗黙の端点として、
// 制御点の間を線形補間する。points は s 昇順。空なら cantStart→cantEnd の線形補間。
float CantAtControlPoints(const std::vector<CantControlPoint>& points, float s, float cantStart,
                          float cantEnd, float totalLen);

// 密なカント列(s昇順)を許容tol以内の折れ線へ間引く。結果が端点2つだけ(=線形)なら空を返す
// (空=台形/線形の既定に任せる)。
std::vector<CantControlPoint> ThinCantSamples(const std::vector<CantControlPoint>& dense,
                                              float tol = kCantThinTolerance);

// 弧長sで二分する。前半はs未満の点、後半はs超過の点(s0を引いた値)。端のカントは
// 呼び出し側が分割点のカントを cantB/cantA に入れるので、分割点自体は点にしない。
std::pair<std::vector<CantControlPoint>, std::vector<CantControlPoint>> SplitCantControlPoints(
    const std::vector<CantControlPoint>& points, float s);

// edge の spline が持つカント列を、進行方向(reversed なら逆向き)に offset を足した弧長で
// out の末尾へ足す。結合で両edgeの有効なカントを連結するときに使う。
void AppendSplineCantSamples(std::vector<CantControlPoint>& out, const TrackEdge& edge,
                             bool reversed, float offset);

}  // namespace rs::world

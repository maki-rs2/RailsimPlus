//
// EdgeEndMismatch.h - edge の端が両端ノードの姿勢と一致しているかの不変条件検査
//
// 全edge(取り込み・敷設・複線中心線・複線メンバーを区別しない)について、
// spline の先頭が nodeA の姿勢、末尾が nodeB の姿勢と一致するかを調べる。
//
#pragma once

#include <vector>


#include "../../core/Epsilon.h"
#include "Guid.h"

namespace rs::world {

struct TrackGraph;

struct EdgeEndMismatch {
    Guid edge = NilGuid();
    bool atStart = true;        // true: nodeA側(spline先頭)、false: nodeB側(spline末尾)
    double positionErrorM = 0.0;
    double directionErrorDeg = 0.0;
};

// 位置が posTolM[m] を超えて、または向きが dirTolDeg[度] を超えてずれている端を列挙する。
// spline が空の edge、端点ノードが無い edge は対象外。
std::vector<EdgeEndMismatch> FindEdgeEndMismatches(const TrackGraph& graph,
                                                   double posTolM = rs::core::kGeometryLengthEpsM,
                                                   double dirTolDeg = 0.01);

}  // namespace rs::world

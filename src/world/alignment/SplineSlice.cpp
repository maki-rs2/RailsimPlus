//
// SplineSlice.cpp - SplineSlice.h の実装
//
#include "SplineSlice.h"

#include <algorithm>

#include "TrackTraversal.h"
#include "TrackEdge.h"

namespace rs::world {

std::vector<TrackNodeSample> SliceSpline(const TrackEdge& edge, double s0In, double s1In) {
    std::vector<TrackNodeSample> out;
    if (edge.spline.empty()) return out;

    const double total = static_cast<double>(edge.spline.back().s);
    // 高速パス: 範囲がedge全体をカバーするならコピーせずそのまま返す。
    if (s0In <= 0.0 && s1In >= total) return edge.spline;

    const double s0 = std::clamp(s0In, 0.0, total);
    const double s1 = std::clamp(s1In, 0.0, total);
    if (s1 <= s0) return out;

    auto makeSample = [&](double s) {
        const TrackFrame f = EvaluateAt(edge, s, +1);
        TrackNodeSample ns;
        ns.pos = f.pos;
        ns.tangent = f.tangent;
        ns.up = f.up;
        ns.cant = f.cant;
        ns.s = static_cast<float>(s);
        return ns;
    };
    out.push_back(makeSample(s0));
    for (const TrackNodeSample& s : edge.spline) {
        if (static_cast<double>(s.s) > s0 && static_cast<double>(s.s) < s1) out.push_back(s);
    }
    out.push_back(makeSample(s1));
    return out;
}

}  // namespace rs::world

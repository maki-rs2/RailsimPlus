#include "CantControlPoints.h"

#include "TrackTraversal.h"

#include <algorithm>
#include <cmath>
#include "TrackEdge.h"

namespace rs::world {

namespace {

// dense[first..last] の端点を結ぶ直線から最も離れた点を探し、tol を超えるなら再帰で残す。
void ThinRange(const std::vector<CantControlPoint>& dense, size_t first, size_t last, float tol,
               std::vector<bool>& keep) {
    if (last <= first + 1) return;
    const CantControlPoint& a = dense[first];
    const CantControlPoint& b = dense[last];
    const float span = b.s - a.s;
    float worst = 0.0f;
    size_t worstIndex = first;
    for (size_t i = first + 1; i < last; ++i) {
        const float t = span > 0.0f ? (dense[i].s - a.s) / span : 0.0f;
        const float err = std::fabs(dense[i].cant - std::lerp(a.cant, b.cant, t));
        if (err > worst) {
            worst = err;
            worstIndex = i;
        }
    }
    if (worst <= tol) return;
    keep[worstIndex] = true;
    ThinRange(dense, first, worstIndex, tol, keep);
    ThinRange(dense, worstIndex, last, tol, keep);
}

}  // namespace

float CantAtControlPoints(const std::vector<CantControlPoint>& points, float s, float cantStart,
                          float cantEnd, float totalLen) {
    CantControlPoint prev{0.0f, cantStart};
    for (const CantControlPoint& next : points) {
        if (s <= next.s) {
            const float span = next.s - prev.s;
            if (span <= 0.0f) return next.cant;
            return std::lerp(prev.cant, next.cant, std::clamp((s - prev.s) / span, 0.0f, 1.0f));
        }
        prev = next;
    }
    const float span = totalLen - prev.s;
    if (span <= 0.0f) return cantEnd;
    return std::lerp(prev.cant, cantEnd, std::clamp((s - prev.s) / span, 0.0f, 1.0f));
}

std::vector<CantControlPoint> ThinCantSamples(const std::vector<CantControlPoint>& dense,
                                              float tol) {
    if (dense.size() < 3) return {};
    std::vector<bool> keep(dense.size(), false);
    keep.front() = keep.back() = true;
    ThinRange(dense, 0, dense.size() - 1, tol, keep);
    std::vector<CantControlPoint> result;
    for (size_t i = 0; i < dense.size(); ++i)
        if (keep[i]) result.push_back(dense[i]);
    if (result.size() <= 2) return {};
    return result;
}

std::pair<std::vector<CantControlPoint>, std::vector<CantControlPoint>> SplitCantControlPoints(
    const std::vector<CantControlPoint>& points, float s) {
    std::pair<std::vector<CantControlPoint>, std::vector<CantControlPoint>> halves;
    for (const CantControlPoint& point : points) {
        if (point.s < s) halves.first.push_back(point);
        else if (point.s > s) halves.second.push_back({point.s - s, point.cant});
    }
    return halves;
}

void AppendSplineCantSamples(std::vector<CantControlPoint>& out, const TrackEdge& edge,
                             bool reversed, float offset) {
    if (edge.spline.empty()) return;
    const float length = static_cast<float>(EdgeLength(edge));
    const auto append = [&](float s, float cant) {
        // 連結点(直前と同じ弧長)は後から来た値を捨てる。
        if (!out.empty() && s <= out.back().s) return;
        out.push_back({s, cant});
    };
    if (!reversed) {
        for (const TrackNodeSample& smp : edge.spline) append(offset + smp.s, smp.cant);
    } else {
        for (auto it = edge.spline.rbegin(); it != edge.spline.rend(); ++it)
            append(offset + (length - it->s), it->cant);
    }
}

}  // namespace rs::world

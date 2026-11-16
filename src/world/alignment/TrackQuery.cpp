#include "TrackQuery.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace rs::world {

namespace {

using rs::core::DVec3;

DVec3 ToD(const rs::core::Vec3& v) { return DVec3{static_cast<double>(v.x), static_cast<double>(v.y), static_cast<double>(v.z)}; }
double LengthSq(const DVec3& a) { return Dot(a, a); }

} // namespace

NearestSplineHit FindNearestSplinePosition(const TrackEdge& edge, const rs::core::Vec3& worldPos,
                                            float verticalOffsetM, double lengthSquaredEpsM2) {
    NearestSplineHit hit;
    const DVec3 target = ToD(worldPos);
    double bestDistSq = std::numeric_limits<double>::max();
    for (size_t index = 1; index < edge.spline.size(); ++index) {
        const TrackNodeSample& sampleA = edge.spline[index - 1];
        const TrackNodeSample& sampleB = edge.spline[index];
        const DVec3 a = ToD(sampleA.pos + sampleA.up * verticalOffsetM);
        const DVec3 b = ToD(sampleB.pos + sampleB.up * verticalOffsetM);
        const double t = rs::core::ClosestSegmentParameter(a, b, target, lengthSquaredEpsM2);
        const DVec3 closest = rs::core::Add(a, rs::core::Scale(rs::core::Sub(b, a), t));
        const double distanceSq = LengthSq(rs::core::Sub(target, closest));
        if (!(distanceSq < bestDistSq)) continue;
        bestDistSq = distanceSq;
        hit.found = true;
        hit.s = static_cast<double>(sampleA.s) + (static_cast<double>(sampleB.s) - sampleA.s) * t;
        hit.tangent = rs::core::Sub(b, a);
        hit.distanceSquaredM2 = distanceSq;
    }
    if (hit.found) hit.distanceM = std::sqrt(bestDistSq);
    return hit;
}

std::optional<double> ProjectOntoEdgeFrom(const TrackEdge& edge, double initialS,
                                          const rs::core::Vec3& worldPos, double maxAlongResidualM) {
    // 曲線でも数回で収束する(各回で接線方向の残差だけ s を進める)。
    constexpr int kMaxIterations = 8;
    if (edge.spline.size() < 2) return std::nullopt;
    const double length = EdgeLength(edge);
    double s = std::clamp(initialS, 0.0, length);
    double along = 0.0;
    for (int i = 0; i < kMaxIterations; ++i) {
        const TrackFrame frame = EvaluateAt(edge, s, +1);
        along = static_cast<double>(rs::core::Vec3::Dot(worldPos - frame.pos, frame.tangent));
        const double next = std::clamp(s + along, 0.0, length);
        if (next == s) break;
        s = next;
    }
    if (std::fabs(along) > maxAlongResidualM) return std::nullopt;
    return s;
}

NearestTrackHit FindNearestTrackPosition(const TrackGraph& graph, const rs::core::Vec3& worldPos,
                                          const rs::core::Vec3& forwardDir, double maxDistanceM) {
    NearestTrackHit hit;
    const DVec3 forwardD = ToD(forwardDir);

    double bestDistSq = std::numeric_limits<double>::max();
    Guid bestEdge = NilGuid();
    double bestS = 0.0;
    DVec3 bestTangent{0.0, 0.0, 1.0};

    for (const std::pair<const Guid, TrackEdge>& entry : graph.edges) {
        const TrackEdge& edge = entry.second;
        if (IsCenterlineOnlyEdge(edge)) continue;
        const NearestSplineHit nearest = FindNearestSplinePosition(edge, worldPos);
        if (!nearest.found) continue;
        const double distanceSq = nearest.distanceSquaredM2;
        if (distanceSq < bestDistSq) {
            bestDistSq = distanceSq;
            bestEdge = entry.first;
            bestS = nearest.s;
            bestTangent = nearest.tangent;
        }
    }

    if (GuidIsNil(bestEdge)) {
        hit.found = false;
        return hit;
    }

    const double bestDist = std::sqrt(bestDistSq);
    if (bestDist > maxDistanceM) {
        hit.found = false;
        return hit;
    }

    hit.position.edge = bestEdge;
    hit.position.s = bestS;
    hit.position.direction = (Dot(bestTangent, forwardD) < 0.0) ? -1 : +1;
    hit.distanceM = bestDist;
    hit.found = true;
    return hit;
}

} // namespace rs::world

//
// PlacementSnap.cpp - 敷設スナップ候補の優先順位と、グラフだけで決まる候補の収集
//
#include "PlacementSnap.h"
#include "../../world/geometry/EditorGeometryTuning.h"

#include "../../core/Epsilon.h"

#include <algorithm>
#include <cmath>

namespace rs::editor {

using rs::core::Vec3;
using rs::world::TrackEdge;

PlacementSnap ChooseSnap(const std::vector<PlacementSnap>& candidates) {
    PlacementSnap best;
    for (const PlacementSnap& candidate : candidates) {
        if (candidate.kind == SnapKind::None) continue;
        const bool stronger = static_cast<int>(candidate.kind) < static_cast<int>(best.kind);
        const bool closer = candidate.kind == best.kind && candidate.distance < best.distance;
        if (stronger || closer) best = candidate;
    }
    return best;
}

std::vector<double> CurveBoundaryS(const TrackEdge& edge) {
    std::vector<double> result;
    for (std::size_t i = 1; i + 1 < edge.spline.size(); ++i) {
        if (edge.spline[i].kind != edge.spline[i - 1].kind) {
            result.push_back(static_cast<double>(edge.spline[i].s));
        }
    }
    return result;
}

bool SnapToCurveBoundary(const TrackEdge& edge, double s, double radiusM, double& outS) {
    bool found = false;
    double bestGap = radiusM;
    for (const double boundary : CurveBoundaryS(edge)) {
        const double gap = std::abs(boundary - s);
        if (gap <= bestGap) {
            bestGap = gap;
            outS = boundary;
            found = true;
        }
    }
    return found;
}

namespace {

// 並走位置に、すでに別のレールがある(水平距離が近い)か。複線の片方の中心は、もう片方の並走位置と
// 重なるので、その位置は新しい線を置く場所として数えない。
bool IsSlotOccupied(const rs::world::TrackGraph& graph, float x, float z) {
    const float kOccupiedM = rs::world::EditorGeometryTuning::Embedded().parallelOccupiedDistanceM;
    for (const std::pair<const rs::world::Guid, TrackEdge>& kv : graph.edges) {
        const TrackEdge& edge = kv.second;
        if (edge.centerlineOnly) continue;
        for (std::size_t i = 1; i < edge.spline.size(); ++i) {
            const Vec3 a = edge.spline[i - 1].pos;
            const Vec3 b = edge.spline[i].pos;
            const float dx = b.x - a.x;
            const float dz = b.z - a.z;
            const float t = static_cast<float>(rs::core::ClosestSegmentParameter(
                {a.x, 0.0, a.z}, {b.x, 0.0, b.z}, {x, 0.0, z},
                rs::core::kHorizontalSegmentLengthSquaredEpsM2));
            const float ex = x - (a.x + dx * t);
            const float ez = z - (a.z + dz * t);
            if (ex * ex + ez * ez <= kOccupiedM * kOccupiedM) return true;
        }
    }
    return false;
}

}  // namespace

bool FindParallelSnap(const rs::world::TrackGraph& graph, const Vec3& hit, float spacingM,
                      float toleranceM, PlacementSnap& out) {
    if (spacingM <= 0.0f) return false;
    // 基準レールの真横から外れた位置(edge端より先)は並走とみなさない。
    const float kMaxAlongM = rs::world::EditorGeometryTuning::Embedded().parallelMaxAlongM;
    bool found = false;
    for (const std::pair<const rs::world::Guid, TrackEdge>& kv : graph.edges) {
        const TrackEdge& edge = kv.second;
        if (edge.centerlineOnly || edge.spline.size() < 2) continue;
        for (std::size_t i = 1; i < edge.spline.size(); ++i) {
            const Vec3 a = edge.spline[i - 1].pos;
            const Vec3 b = edge.spline[i].pos;
            const float dx = b.x - a.x;
            const float dz = b.z - a.z;
            const float lenSq = dx * dx + dz * dz;
            if (lenSq < rs::core::kHorizontalSegmentLengthSquaredEpsM2) continue;
            const float len = std::sqrt(lenSq);
            const float tx = dx / len;
            const float tz = dz / len;
            const float t = static_cast<float>(rs::core::ClosestSegmentParameter(
                {a.x, 0.0, a.z}, {b.x, 0.0, b.z}, {hit.x, 0.0, hit.z},
                rs::core::kHorizontalSegmentLengthSquaredEpsM2));
            const float qx = a.x + dx * t;
            const float qz = a.z + dz * t;
            const float rx = hit.x - qx;
            const float rz = hit.z - qz;
            const float along = rx * tx + rz * tz;
            if (std::abs(along) > kMaxAlongM) continue;
            // 進行方向の右手(cross(up, tangent)と同じ向き)への横ずれ。
            const float lateral = rx * tz - rz * tx;
            for (const float sign : {1.0f, -1.0f}) {
                const float gap = std::abs(lateral - sign * spacingM);
                if (gap > toleranceM || (found && gap >= out.distance)) continue;
                if (IsSlotOccupied(graph, qx + tz * sign * spacingM, qz - tx * sign * spacingM)) continue;
                out.kind = SnapKind::Parallel;
                out.distance = gap;
                out.edge = kv.first;
                out.edgeS = static_cast<double>(edge.spline[i - 1].s) +
                            static_cast<double>(edge.spline[i].s - edge.spline[i - 1].s) * t;
                out.pos = Vec3(qx + tz * sign * spacingM, hit.y, qz - tx * sign * spacingM);
                found = true;
            }
        }
    }
    return found;
}

} // namespace rs::editor

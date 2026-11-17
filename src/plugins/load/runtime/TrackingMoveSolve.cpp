#include "TrackingMoveSolve.h"

#include <cmath>
#include <limits>

namespace rs::legacy_plugin {

using namespace rs::core;

TrackingMoveResult SolveTrackingMove(const TrackingWorldMatrix& world,
    const rs::legacy_pi::TrackingMoveCustomizer& move, const std::vector<TrackingSegment>& segments) {
    TrackingMoveResult result{false, {move.displacement[0], move.displacement[1], move.displacement[2]}};
    const DVec3 x{world[0], world[1], world[2]};
    const DVec3 y{world[4], world[5], world[6]};
    const DVec3 z{world[8], world[9], world[10]};
    const DVec3 t{world[12], world[13], world[14]};
    const double width = move.sliderHalfRange > 0.0f ? move.sliderHalfRange : move.ascentRange;
    const DVec3 a = Add(t, Scale(x, width));
    const DVec3 b = Sub(t, Scale(x, width));
    const DVec3 p = Add(t, Add(Scale(x, move.displacement[0]),
                              Add(Scale(y, move.displacement[1]), Scale(z, move.displacement[2]))));
    const DVec3 ab = Sub(a, b);
    const double abSquared = Dot(ab, ab);
    const DVec3 normal = Normalize(Cross(ab, Sub(a, p)));
    if (abSquared == 0.0 || Dot(normal, normal) == 0.0) return result;
    // RailSim II の初期距離と XMPlaneIntersectLine の平行判定に合わせる。
    double minHeight = 100.0;
    constexpr double kPlaneParallelEps = std::numeric_limits<float>::epsilon();
    for (const TrackingSegment& segment : segments) {
        const DVec3 reverse = Sub(segment.start, segment.end);
        const double denominator = Dot(normal, reverse);
        if (std::abs(denominator) <= kPlaneParallelEps) continue;
        const DVec3 intersection = Add(segment.start,
            Scale(reverse, Dot(normal, Sub(a, segment.start)) / denominator));
        if (Dot(Sub(intersection, segment.start), Sub(intersection, segment.end)) > 0.0) continue;
        const DVec3 foot = Add(b, Scale(ab, Dot(Sub(intersection, b), ab) / abSquared));
        const double height = Length(Sub(intersection, foot));
        const bool inRange = move.sliderHalfRange > 0.0f
            ? Dot(Sub(foot, a), Sub(foot, b)) < 0.0 && height <= move.ascentRange
            : Length(Sub(intersection, t)) <= move.ascentRange;
        if (!inRange || height >= minHeight) continue;
        minHeight = height;
        const DVec3 offset = Sub(intersection, t);
        result = {true, {move.sliderHalfRange > 0.0f ? 0.0 : Dot(offset, x),
                         Dot(offset, y), Dot(offset, z)}};
    }
    return result;
}

TrackingWorldMatrix ComposeTrackingWorld(const Mat4& local, const Mat4& base,
                                         const Mat4& placementToWorld) {
    TrackingWorldMatrix intermediate{}, world{};
    for (size_t row = 0; row < 4; ++row)
        for (size_t col = 0; col < 4; ++col)
            for (size_t k = 0; k < 4; ++k)
                intermediate[row * 4 + col] += static_cast<double>(local.m[row * 4 + k]) * base.m[k * 4 + col];
    for (size_t row = 0; row < 4; ++row)
        for (size_t col = 0; col < 4; ++col)
            for (size_t k = 0; k < 4; ++k)
                world[row * 4 + col] += intermediate[row * 4 + k] * placementToWorld.m[k * 4 + col];
    return world;
}

} // namespace rs::legacy_plugin

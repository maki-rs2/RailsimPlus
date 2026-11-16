// SceneryTransformMath.h - 情景ギズモの座標変換で共有する純粋計算
#pragma once

#include <array>
#include <cmath>

#include "../../core/Math.h"

#include "../../core/Epsilon.h"
#include "../../world/alignment/Alignment.h"

namespace rs::app {

inline std::array<rs::core::Vec3, 3> SceneryGizmoAxes(rs::core::Vec3 forward) {
    forward.y = 0.0f;
    if (forward.Length() < rs::core::kHorizontalDirectionEps) forward = {0.0f, 0.0f, 1.0f};
    forward = forward.Normalized();
    const rs::core::Vec3 up(0.0f, 1.0f, 0.0f);
    return {rs::core::Vec3::Cross(up, forward).Normalized(), up, forward};
}

inline double SnapSceneryTransformDelta(double delta, double step) {
    return step > 0.0 ? std::round(delta / step) * step : delta;
}

inline rs::core::Vec3 TransformSceneryPosition(const rs::core::Vec3& position,
                                               const rs::core::Vec3& pivot,
                                               const rs::core::Vec3& axis,
                                               bool rotation, float radians) {
    if (!rotation) return position + axis * radians;
    return pivot + rs::world::RotateAroundAxis(position - pivot, axis, radians);
}

} // namespace rs::app

#pragma once

#include "../core/Math.h"

namespace rs::render {

// 描画用の定数・頂点・姿勢へ詰めるときの縮小変換を共通にする。計算中の値には使わない。
inline constexpr float ToGpuFloat(double value) {
    return static_cast<float>(value);
}

inline rs::core::Vec3 ToGpuFloat(const rs::core::DVec3& value) {
    return {ToGpuFloat(value.x), ToGpuFloat(value.y), ToGpuFloat(value.z)};
}

} // namespace rs::render

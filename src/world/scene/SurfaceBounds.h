//
// SurfaceBounds.h - Surface(地形)プラグインの範囲(原点を中心にしたXZ平面の長方形)
//
// 範囲の外へ出る座標を縁へ押し戻す。RailSim II の CSurfacePlugin::ClipRect と同じ規則。Yは見ない。
//
#pragma once

#include "../../core/Math.h"

namespace rs::world {

struct SurfaceBounds {
    float sizeX = 0.0f;  // X(東西)の全幅[m]
    float sizeZ = 0.0f;  // Z(南北)の全幅[m]
};

// posを範囲内へ押し戻す。範囲内だったときだけtrueを返す。
inline bool ClipToSurfaceBounds(const SurfaceBounds& bounds, rs::core::Vec3& pos) {
    const float halfX = 0.5f * bounds.sizeX;
    const float halfZ = 0.5f * bounds.sizeZ;
    bool inside = true;
    if (pos.x < -halfX) { pos.x = -halfX; inside = false; }
    else if (pos.x > halfX) { pos.x = halfX; inside = false; }
    if (pos.z < -halfZ) { pos.z = -halfZ; inside = false; }
    else if (pos.z > halfZ) { pos.z = halfZ; inside = false; }
    return inside;
}

}  // namespace rs::world

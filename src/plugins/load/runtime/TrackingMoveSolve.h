#pragma once

#include <array>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "../../../core/Math.h"
#include "../../schema/TrackingCustomizerSchema.h"

namespace rs::legacy_plugin {

using TrackingWorldMatrix = std::array<double, 16>;

struct TrackingSegment {
    rs::core::DVec3 start;
    rs::core::DVec3 end;
};

using TrackingTargetTable = std::unordered_map<std::string, std::vector<TrackingSegment>>;

struct TrackingMoveResult {
    bool found = false;
    rs::core::DVec3 displacement;
};

struct TrackingMoveContext {
    const TrackingTargetTable* targets = nullptr;
    // アンカーが無いパーツは false を返し、既定の変位を使う。
    std::function<bool(size_t, rs::core::Mat4&)> placementToWorld;
};

TrackingMoveResult SolveTrackingMove(const TrackingWorldMatrix& world,
    const rs::legacy_pi::TrackingMoveCustomizer& move, const std::vector<TrackingSegment>& segments);

// 既存の描画用行列を受け取り、追従前の合成だけは double で行う。
TrackingWorldMatrix ComposeTrackingWorld(const rs::core::Mat4& local,
    const rs::core::Mat4& base, const rs::core::Mat4& placementToWorld);

} // namespace rs::legacy_plugin

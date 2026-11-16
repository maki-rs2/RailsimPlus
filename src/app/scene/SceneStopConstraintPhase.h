#pragma once

#include <optional>
#include <functional>
#include <unordered_set>
#include <vector>

#include "../../sim/BlockOccupancy.h"
#include "../../sim/TrainRuntime.h"
#include "../../world/alignment/TrackGraph.h"
#include "../../world/alignment/WarpLinkTable.h"

namespace rs::app {

using rs::world::TrainId;

// MovingBlockAhead/PointGuard/DeadEnd/SceneBoundaryの停止制約を
// 1グループぶんまとめて計算し、runtimeへSetGroupStopConstraint/ClearGroupStopConstraintを発行する。
// SceneTrainController::Tick()が長くなりすぎないよう、別関数にしている。
void RunStopConstraintPhase(
    const std::function<const rs::world::TrackGraph*(size_t)>& graphForScene,
    const rs::world::WarpLinkTable& warpTable, TrainId trainId,
    rs::sim::TrainRuntime& runtime,
    const std::vector<rs::sim::OccupancyTrain>& occupancyTrains,
    const std::unordered_set<rs::world::Guid, rs::world::GuidHash>& blocksOccupiedByOthers,
    double movingBlockSeekAheadM,
    const rs::world::ForwardScanSpan* forwardSpan = nullptr,
    const rs::world::NodeSlotOverride* slotOverride = nullptr, double dtSeconds = 0.0);

} // namespace rs::app

#include "SceneStopConstraintPhase.h"
#include "../../sim/SimTuning.h"

#include <algorithm>

#include "../../world/alignment/PointRoutineResolve.h"

namespace rs::app {

void RunStopConstraintPhase(
    const std::function<const rs::world::TrackGraph*(size_t)>& graphForScene,
    const rs::world::WarpLinkTable& warpTable, TrainId trainId,
    rs::sim::TrainRuntime& runtime,
    const std::vector<rs::sim::OccupancyTrain>& occupancyTrains,
    const std::unordered_set<rs::world::Guid, rs::world::GuidHash>& blocksOccupiedByOthers,
    double movingBlockSeekAheadM,
    const rs::world::ForwardScanSpan* forwardSpan, const rs::world::NodeSlotOverride* slotOverride,
    double dtSeconds) {
    const std::optional<double> movingBlockStopDistanceM = runtime.ComputeMovingBlockConstraint(
        graphForScene, warpTable, trainId, occupancyTrains, movingBlockSeekAheadM,
        forwardSpan, slotOverride, dtSeconds);
    if (movingBlockStopDistanceM.has_value()) {
        runtime.SetGroupStopConstraint(trainId,
                                       rs::sim::StopConstraintSource::MovingBlockAhead,
                                       *movingBlockStopDistanceM);
    } else {
        runtime.ClearGroupStopConstraint(trainId,
                                         rs::sim::StopConstraintSource::MovingBlockAhead);
    }

    // ポイント在線ガードで転換できず、古い開通の先が他編成の閉塞に入る分岐の手前で止まる。
    std::optional<double> pointGuardStopAheadM;
    if (forwardSpan != nullptr) {
        pointGuardStopAheadM = rs::world::FindGuardedPointStopAheadM(
            graphForScene, *forwardSpan, blocksOccupiedByOthers);
    }
    if (pointGuardStopAheadM.has_value()) {
        runtime.SetGroupStopConstraint(
            trainId, rs::sim::StopConstraintSource::PointGuard,
            std::max(0.0, *pointGuardStopAheadM -
                              rs::sim::SimTuning::Embedded().signalMargins.pointGuardStopMarginM));
    } else {
        runtime.ClearGroupStopConstraint(trainId, rs::sim::StopConstraintSource::PointGuard);
    }

    // 前方走査が行き止まり/走行不能なシーン境界で終端していれば、その手前で止まれるよう
    // 制約を張る。entriesが空(走査開始位置がグラフに無い等のデータ不整合)のときは張らない。
    // 0mの制約が残ったままになると、その編成は以後どちらへも動けなくなるため。
    const bool spanAtEnd = forwardSpan != nullptr && !forwardSpan->entries.empty() &&
                           forwardSpan->termination.atEnd;
    const rs::world::ScanEndReason endReason =
        spanAtEnd ? forwardSpan->termination.reason : rs::world::ScanEndReason::None;
    const double endStopDistanceM =
        spanAtEnd ? std::max(0.0, forwardSpan->scannedM -
                                      rs::sim::SimTuning::Embedded().deadEndStop.marginM)
                  : 0.0;

    if (endReason == rs::world::ScanEndReason::DeadEnd) {
        runtime.SetGroupStopConstraint(trainId, rs::sim::StopConstraintSource::DeadEnd,
                                       endStopDistanceM);
    } else {
        runtime.ClearGroupStopConstraint(trainId, rs::sim::StopConstraintSource::DeadEnd);
    }
    if (endReason == rs::world::ScanEndReason::SceneBoundary) {
        runtime.SetGroupStopConstraint(trainId, rs::sim::StopConstraintSource::SceneBoundary,
                                       endStopDistanceM);
    } else {
        runtime.ClearGroupStopConstraint(trainId,
                                         rs::sim::StopConstraintSource::SceneBoundary);
    }

}

} // namespace rs::app

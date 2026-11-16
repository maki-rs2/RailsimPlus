//
// TrainRuntime.cpp
//
#include "TrainRuntime.h"
#include "../core/SpeedUnits.h"
#include "SimTuning.h"

#include "../world/alignment/PointRoutineResolve.h"

#include <algorithm>
#include <cmath>

#include <limits>
#include <unordered_set>

#include "../world/alignment/PlatformChain.h"
#include "../world/alignment/PlatformRegistry.h"
#include "../world/alignment/WarpTraversal.h"

namespace {
}

namespace rs::sim {

bool TrainRuntime::TravelReversed(const RunningTrain& running) {
    if (running.kin.speed != 0.0) return running.kin.speed < 0.0;
    if (running.kin.targetSpeed != 0.0) return running.kin.targetSpeed < 0.0;
    return running.lastTravelReversed;
}

void TrainRuntime::ReleaseDepartedPlatformIfLeft(
    const GraphForScene& graphForScene, const rs::world::WarpLinkTable& warpTable,
    RunningTrain& running, double trainLengthM) {
    if (running.departedPlatform != rs::world::NilGuid()) {
        if (!IsPlatformOccupied(graphForScene, warpTable, running.sceneIndex, running.kin.head,
                                running.occupiedPath, trainLengthM, running.departedPlatform)) {
            running.departedPlatform = rs::world::NilGuid();
        }
    }
}


void TrainRuntime::SetGroupTargetSpeed(TrainId trainId, double targetSpeedKmh) {
    // targetSpeedKmh==0.0だけを「明示的な停止」として特別扱いする(未配置groupIdに
    // 対しては新規エントリを作らない)。0以外(負値=後退方向も含む)は通常どおり
    // エントリを作成/更新する(負の目標速度=後退)。
    if (targetSpeedKmh == 0.0) {
        const std::map<TrainId, RunningTrain>::iterator it = runningGroups_.find(trainId);
        if (it != runningGroups_.end()) {
            it->second.kin.targetSpeed = 0.0;
            // speed==0まで自然減速させたいので即消去はしない
            // (次tickでspeedが0へクランプされた後もheadは維持され続ける)。
        }
        return;
    }
    RunningTrain& running = runningGroups_[trainId];
    running.kin.targetSpeed = rs::core::KilometersPerHourToMetersPerSecond(targetSpeedKmh);
}

void TrainRuntime::SetGroupCurrentSpeed(TrainId trainId, double speedKmh) {
    // 取込/強制停止という明示操作だけが呼ぶため、0も即時停止の意図としてエントリを作成する。
    RunningTrain& running = runningGroups_[trainId];
    running.kin.speed = rs::core::KilometersPerHourToMetersPerSecond(speedKmh);
}

double TrainRuntime::GroupTargetSpeedKmh(TrainId trainId) const {
    const std::map<TrainId, RunningTrain>::const_iterator it = runningGroups_.find(trainId);
    return it == runningGroups_.end() ? 0.0 : rs::core::MetersPerSecondToKilometersPerHour(it->second.kin.targetSpeed);
}

double TrainRuntime::GroupCurrentSpeedKmh(TrainId trainId) const {
    const std::map<TrainId, RunningTrain>::const_iterator it = runningGroups_.find(trainId);
    return it == runningGroups_.end() ? 0.0 : rs::core::MetersPerSecondToKilometersPerHour(it->second.kin.speed);
}

bool TrainRuntime::GroupLastTravelReversed(TrainId trainId) const {
    const std::map<TrainId, RunningTrain>::const_iterator it = runningGroups_.find(trainId);
    return it != runningGroups_.end() && it->second.lastTravelReversed;
}

double TrainRuntime::GroupOdometerM(TrainId trainId) const {
    const std::map<TrainId, RunningTrain>::const_iterator it = runningGroups_.find(trainId);
    return it == runningGroups_.end() ? 0.0 : it->second.odometerM;
}

bool TrainRuntime::GroupTravelReversed(TrainId trainId) const {
    const std::map<TrainId, RunningTrain>::const_iterator it = runningGroups_.find(trainId);
    return it != runningGroups_.end() && TravelReversed(it->second);
}

bool TrainRuntime::GetGroupForwardScanStart(const GraphForScene& graphForScene,
                                            const rs::world::WarpLinkTable& warpTable,
                                            TrainId trainId, LeadingEnd& outLeadingEnd) const {
    const std::map<TrainId, RunningTrain>::const_iterator it = runningGroups_.find(trainId);
    if (it == runningGroups_.end() || !it->second.kin.started) return false;
    outLeadingEnd = ResolveLeadingEnd(graphForScene, warpTable, it->second.sceneIndex,
                                      it->second.kin.head, TravelReversed(it->second),
                                      it->second.occupiedPath, it->second.occupiedPathKeepLengthM);
    return true;
}

void TrainRuntime::ResolveGroupSeekAheadPointRoutines(
    const std::function<rs::world::TrackGraph*(size_t)>& graphForScene,
    const rs::world::WarpLinkTable& warpTable, TrainId trainId,
    const rs::world::Guid& formationId, size_t startScene,
    const rs::world::TrackPosition& start, double ds,
    const std::unordered_set<rs::world::Guid, rs::world::GuidHash>& occupiedBlocks,
    const std::vector<int>& trainTagIds, double nowAbsSeconds,
    const std::unordered_set<rs::world::Guid, rs::world::GuidHash>& previousOccupiedBlocks) {
    rs::world::PointConditionContext conditionContext;
    conditionContext.trainId = trainId;
    conditionContext.formationId = formationId;
    conditionContext.trainTagIds = &trainTagIds;
    conditionContext.previousOccupiedBlocks = &previousOccupiedBlocks;
    conditionContext.nowAbsSeconds = nowAbsSeconds;
    rs::world::ResolveSeekAheadPointRoutines(graphForScene, warpTable, trainId, startScene, start,
                                             ds, pointRoutineCursor_, occupiedBlocks,
                                             conditionContext);
}

const rs::world::NodeSlotOverride* TrainRuntime::GroupSeekDecisions(TrainId trainId) const {
    const std::unordered_map<TrainId, rs::world::NodeSlotOverride, rs::world::GuidHash>::const_iterator
        it = pointRoutineCursor_.seekDecisions.find(trainId);
    return it == pointRoutineCursor_.seekDecisions.end() ? nullptr : &it->second;
}

double TrainRuntime::GroupCurrentAccelMhPerS(TrainId trainId) const {
    const std::map<TrainId, RunningTrain>::const_iterator it = runningGroups_.find(trainId);
    return it == runningGroups_.end() ? 0.0 : it->second.accelMhPerS;
}

void TrainRuntime::SetGroupStopDistance(TrainId trainId, double remainingMeters) {
    RunningTrain& running = runningGroups_[trainId];
    // 負値はこのクランプで0になり「その場で即停止」に化ける。呼び出し側が負を
    // 渡していないかは、ここではなく算出側(SeekStationStop)で確認すること。
    running.kin.stopDistance = std::max(0.0, remainingMeters);
    running.kin.arrivedAtStop = false;
    // 停止目標が生まれる唯一の合流点なので、対応する走行方向のスナップショットも
    // ここで採る(SeekStationStop側で採ると値と食い違いうる)。
    running.stopTargetReversed = TravelReversed(running);
}

void TrainRuntime::SetGroupStopConstraint(TrainId trainId,
                                            rs::sim::StopConstraintSource source,
                                            double remainingMeters) {
    if (source == rs::sim::StopConstraintSource::StationStop) return;
    RunningTrain& running = runningGroups_[trainId];
    running.stopConstraints[static_cast<size_t>(source)] = std::max(0.0, remainingMeters);
}

void TrainRuntime::ClearGroupStopConstraint(TrainId trainId,
                                              rs::sim::StopConstraintSource source) {
    if (source == rs::sim::StopConstraintSource::StationStop) return;
    const std::map<TrainId, RunningTrain>::iterator it = runningGroups_.find(trainId);
    if (it == runningGroups_.end()) return;
    it->second.stopConstraints[static_cast<size_t>(source)] = std::nullopt;
}

void TrainRuntime::SetGroupSpeedLimitConstraints(
    TrainId trainId, std::vector<rs::sim::SpeedConstraint> constraints) {
    const std::map<TrainId, RunningTrain>::iterator it = runningGroups_.find(trainId);
    if (it == runningGroups_.end()) return;
    it->second.speedLimitConstraints = std::move(constraints);
}

void TrainRuntime::ClearGroupStopTarget(TrainId trainId) {
    const std::map<TrainId, RunningTrain>::iterator it = runningGroups_.find(trainId);
    if (it == runningGroups_.end()) return;
    it->second.kin.stopDistance.reset();
    it->second.kin.arrivedAtStop = false;
    // targetPlatformもここで解除する。解除しないとSeekStationStopの
    // 「既にターゲット済み」ガード(platformId == running.targetPlatform)が
    // 発車後も残り続け、環状線などで同じ番線へ再度戻ってきた編成が
    // 二度目の減速を一切しなくなる。
    // 直前の停止目標が番線だった場合、その番線を「発車済み」として覚えておく。
    // 発車直後はheadがまだ番線edge上にいるため、これが無いとSeekStationStopが
    // 同じ番線を距離0で再捕捉し、その場で停車したまま二度と発車できなくなる
    // (DiaRuntimeは同じ番線への再到着へ発車予定を組まない)。
    it->second.departedPlatform = it->second.targetPlatform;
    it->second.targetPlatform = rs::world::NilGuid();
}

bool TrainRuntime::GroupHasStopTarget(TrainId trainId) const {
    const std::map<TrainId, RunningTrain>::const_iterator it = runningGroups_.find(trainId);
    return it != runningGroups_.end() && it->second.kin.stopDistance.has_value();
}

std::optional<double> TrainRuntime::GroupStopDistanceM(TrainId trainId) const {
    const std::map<TrainId, RunningTrain>::const_iterator it = runningGroups_.find(trainId);
    if (it == runningGroups_.end()) return std::nullopt;
    return it->second.kin.stopDistance;
}

bool TrainRuntime::GroupArrivedAtStop(TrainId trainId) const {
    const std::map<TrainId, RunningTrain>::const_iterator it = runningGroups_.find(trainId);
    return it != runningGroups_.end() && it->second.kin.arrivedAtStop;
}

rs::world::Guid TrainRuntime::GroupTargetPlatform(TrainId trainId) const {
    const std::map<TrainId, RunningTrain>::const_iterator it = runningGroups_.find(trainId);
    return it == runningGroups_.end() ? rs::world::NilGuid() : it->second.targetPlatform;
}

std::optional<size_t> TrainRuntime::GroupSceneIndex(TrainId trainId) const {
    const std::map<TrainId, RunningTrain>::const_iterator it = runningGroups_.find(trainId);
    if (it == runningGroups_.end() || !it->second.kin.started) return std::nullopt;
    return it->second.sceneIndex;
}

void TrainRuntime::SetGroupOccupiedPathKeepLength(TrainId trainId, double meters) {
    runningGroups_[trainId].occupiedPathKeepLengthM = std::max(0.0, meters);
}

const TrainOccupiedPath* TrainRuntime::GroupOccupiedPath(TrainId trainId) const {
    const std::map<TrainId, RunningTrain>::const_iterator it = runningGroups_.find(trainId);
    return it == runningGroups_.end() || it->second.occupiedPath.entries.empty()
        ? nullptr : &it->second.occupiedPath;
}

std::vector<const TrainOccupiedPath*> TrainRuntime::AllOccupiedPaths() const {
    std::vector<const TrainOccupiedPath*> paths;
    paths.reserve(runningGroups_.size());
    for (const std::pair<const TrainId, RunningTrain>& entry : runningGroups_)
        if (!entry.second.occupiedPath.entries.empty()) paths.push_back(&entry.second.occupiedPath);
    return paths;
}

void TrainRuntime::RemoveGroup(TrainId trainId) {
    runningGroups_.erase(trainId);
    pointRoutineCursor_.RemoveTrain(trainId);
}

} // namespace rs::sim

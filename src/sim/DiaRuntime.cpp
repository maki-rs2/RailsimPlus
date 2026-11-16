#include "DiaRuntime.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "../world/time/GameClock.h"

namespace rs::sim {

namespace {

// RailSim II の内部時間の単位(1秒あたりのフレーム数)。
constexpr double kRailsimFramesPerSecond = 30.0;

// 到着時刻から、この停車の発車予定絶対秒を1回だけ確定させる。
double ComputeDepartureAbsSeconds(const DiaStopSpec& spec, double arrivalAbsSeconds) {
    if (spec.timeType == DiaStopSpec::TimeType::AbsoluteTimeOfDay) {
        // 既に過ぎた時刻は翌日へ送られる。日跨ぎの停車をここで吸収する。
        if (spec.departureTimes.empty()) return arrivalAbsSeconds;
        double earliest = std::numeric_limits<double>::max();
        for (const rs::world::DiaTimeOfDay& time : spec.departureTimes) {
            earliest = std::min(earliest, rs::world::NextAbsoluteSecondsAt(
                arrivalAbsSeconds, time.hour, time.minute, time.second));
        }
        return earliest;
    }
    return arrivalAbsSeconds + std::max(0.0, spec.dwellSeconds);
}

} // namespace

double DiaRuntime::DoorCloseLeadSeconds(double doorClosingTimeSeconds, bool doorsOpen,
                                        double timeScale) {
    if (!doorsOpen) return 0.0;
    // RailSim II はフレーム数(30fps)へ丸めて持つ。ゲーム内秒にはTimeScaleを掛ける。
    const double frames = std::round(std::max(0.0, doorClosingTimeSeconds) * kRailsimFramesPerSecond);
    return frames / kRailsimFramesPerSecond * std::max(0.0, timeScale);
}

DiaDepartureSchedule DiaRuntime::ScheduleDeparture(const DiaStopSpec& spec,
                                                   double arrivalAbsSeconds,
                                                   double doorCloseLeadSeconds) {
    DiaDepartureSchedule schedule;
    schedule.departureAbsSeconds = ComputeDepartureAbsSeconds(spec, arrivalAbsSeconds);
    schedule.doorCloseAbsSeconds = std::max(
        arrivalAbsSeconds,
        schedule.departureAbsSeconds - std::max(0.0, doorCloseLeadSeconds));
    return schedule;
}

DiaStopSpec DiaRuntime::ApplyRoutineElement(DiaStopSpec base,
                                            const rs::world::PlatformRoutineElement& element) {
    base.timeType = element.timeType == rs::world::DiaTimeType::AbsoluteTimeOfDay
        ? DiaStopSpec::TimeType::AbsoluteTimeOfDay
        : DiaStopSpec::TimeType::ElapsedFromArrival;
    base.dwellSeconds = element.dwellSeconds;
    base.departureTimes = element.departureTimes;
    base.action = element.action;
    base.switchChanges = element.switchChanges;
    base.runInfoChanges = element.runInfoChanges;
    return base;
}

DiaRuntimeGroupSnapshot DiaRuntime::MakeArrivedSnapshot(TrainId trainId, DiaState state,
                                                        const rs::world::Guid& platform,
                                                        const DiaStopSpec& spec,
                                                        double nowAbsSeconds,
                                                        double doorCloseLeadSeconds) {
    DiaDepartureSchedule schedule = ScheduleDeparture(spec, nowAbsSeconds, doorCloseLeadSeconds);
    if (state == DiaState::Departing) {
        // ドアを閉じ始めている段階なので、発車は先行時間の内に収める。
        schedule.departureAbsSeconds = std::min(
            schedule.departureAbsSeconds, nowAbsSeconds + std::max(0.0, doorCloseLeadSeconds));
        schedule.doorCloseAbsSeconds = nowAbsSeconds;
    } else {
        state = DiaState::Stopped;
    }
    DiaRuntimeGroupSnapshot snapshot;
    snapshot.trainId = trainId;
    snapshot.state = state;
    snapshot.visitPlatform = platform;
    snapshot.departureScheduled = true;
    snapshot.departureAbsSeconds = schedule.departureAbsSeconds;
    snapshot.doorCloseAbsSeconds = schedule.doorCloseAbsSeconds;
    // 折り返しは到着時にすでに目標速度の符号へ反映済みなので、発車時にもう一度反転させない。
    snapshot.currentAction = rs::world::DiaAction::Stop;
    snapshot.currentSwitchChanges = spec.switchChanges;
    return snapshot;
}

void DiaRuntime::SetGroupStopSpec(TrainId trainId, const DiaStopSpec& spec) {
    groups_[trainId].spec = spec;
}

DiaTickResult DiaRuntime::Tick(TrainId trainId, const DiaTickInput& input,
                               double nowAbsSeconds) {
    GroupRun& run = groups_[trainId];
    if (run.lastPhaseSampleAbsSeconds != 0.0) {
        run.phaseElapsed += std::max(0.0, nowAbsSeconds - run.lastPhaseSampleAbsSeconds);
    }
    run.lastPhaseSampleAbsSeconds = nowAbsSeconds;
    bool runInfoJustApplied = false;

    if (!input.arrivedAtStop) {
        // 停車していない間は走行/減速中のどちらか。停車サイクルの途中で停止目標が
        // 外部から解除された場合もここへ落ちて畳まれる。
        const DiaState nextState = input.hasStopTarget ? DiaState::Braking : DiaState::Running;
        if (run.state != nextState) run.phaseElapsed = 0.0;
        run.state = nextState;
        run.visitPlatform = rs::world::NilGuid();
        run.departureScheduled = false;
        if (!input.hasStopTarget) run.departedPlatform = rs::world::NilGuid();
        DiaTickResult result;
        result.state = run.state;
        return result;
    }

    if (run.state != DiaState::Stopped && run.state != DiaState::Departing) {
        // 到着したtick。発車予定はここで1回だけ確定させ、以後のtickでは触らない。
        run.state = DiaState::Stopped;
        run.phaseElapsed = 0.0;
        run.visitPlatform = input.targetPlatform;
        run.departureScheduled = false;
        // 番線を伴わない停止(UIからの手動停止距離指定)では発車予定を組まない。
        // targetPlatformは発車後もラッチされたまま残るので、直前に発車した番線と
        // 同じなら「その番線への新しい到着」ではないと判断する。
        const bool arrivedAtNewPlatform = input.targetPlatform != rs::world::NilGuid() &&
                                          input.targetPlatform != run.departedPlatform;
        if (arrivedAtNewPlatform) {
            DiaStopSpec effectiveSpec = run.spec;
            if (input.targetPlatformInfo != nullptr) {
                const rs::world::ResolvedPlatformRoutine resolved = rs::world::ResolveTrainRoutine(
                    input.targetPlatformInfo->trainRoutines,
                    input.targetPlatformInfo->formationRoutines,
                    input.targetPlatformInfo->tagRoutines,
                    input.targetPlatformInfo->routine, trainId,
                    tagRegistry_.FormationOfTrain(trainId),
                    tagRegistry_.TrainTags(trainId));
                int& cursor = rs::world::GuidIsNil(resolved.trainId)
                    ? routineCursor_[input.targetPlatform][resolved.layerKey]
                    : trainRoutineCursor_[input.targetPlatform][resolved.trainId];
                const rs::world::PlatformRoutineElement* element =
                    rs::world::DequeueRoutineElement(*resolved.routine, cursor);
                if (element != nullptr) {
                    effectiveSpec = ApplyRoutineElement(effectiveSpec, *element);
                }
            }
            const DiaDepartureSchedule schedule = ScheduleDeparture(
                effectiveSpec, nowAbsSeconds, input.doorCloseLeadSeconds);
            run.departureAbsSeconds = schedule.departureAbsSeconds;
            run.doorCloseAbsSeconds = schedule.doorCloseAbsSeconds;
            run.departureScheduled = true;
            run.currentAction = effectiveSpec.action;
            run.currentSwitchChanges = effectiveSpec.switchChanges;
            // スイッチ上書きは停車中に継続適用するが、運行情報は到着時に一度だけ書き換える。
            if (!effectiveSpec.runInfoChanges.empty()) {
                const rs::world::TrainRunInfo* base = runInfoRegistry_.RunInfo(trainId);
                const rs::world::TrainRunInfo baseInfo =
                    base != nullptr ? *base : rs::world::TrainRunInfo{};
                runInfoRegistry_.SetRunInfo(
                    trainId,
                    rs::world::ApplyRunInfoChanges(baseInfo, effectiveSpec.runInfoChanges));
                runInfoJustApplied = true;
            }
        }
    }

    DiaTickResult result;
    result.runInfoJustApplied = runInfoJustApplied;
    // 遷移は同一tick内で連鎖させる(else ifにしない)。高TimeScaleでは1tickの
    // ゲーム内経過がドア閉じ時間を丸ごと飛び越えうる。
    if (run.state == DiaState::Stopped && run.departureScheduled &&
        nowAbsSeconds >= run.doorCloseAbsSeconds) {
        run.state = DiaState::Departing;
        run.phaseElapsed = 0.0;
    }
    if (run.state == DiaState::Departing && nowAbsSeconds >= run.departureAbsSeconds) {
        run.state = DiaState::Running;
        run.phaseElapsed = 0.0;
        run.departedPlatform = run.visitPlatform;
        run.visitPlatform = rs::world::NilGuid();
        run.departureScheduled = false;
        // 直近発車駅はダイヤ作者が書けるフィールドではなく、実走行からエンジンが
        // 算出する。発車tickの番線から所属駅を引いて上書きする。
        if (input.targetPlatformInfo != nullptr &&
            !rs::world::GuidIsNil(input.targetPlatformInfo->stationId)) {
            const rs::world::TrainRunInfo* base = runInfoRegistry_.RunInfo(trainId);
            rs::world::TrainRunInfo updated = base != nullptr ? *base : rs::world::TrainRunInfo{};
            updated.lastDepartureStation = input.targetPlatformInfo->stationId;
            runInfoRegistry_.SetRunInfo(trainId, std::move(updated));
        }
        result.departNow = true;
        result.departAction = run.currentAction;
    }
    result.state = run.state;
    result.doorsOpen = run.state == DiaState::Stopped;
    result.activeSwitchChanges = run.state == DiaState::Stopped
        ? run.currentSwitchChanges
        : std::vector<rs::world::SwitchChange>{};
    return result;
}

DiaState DiaRuntime::GroupState(TrainId trainId) const {
    const std::map<TrainId, GroupRun>::const_iterator it = groups_.find(trainId);
    return it == groups_.end() ? DiaState::Running : it->second.state;
}

rs::world::Guid DiaRuntime::GroupVisitPlatform(TrainId trainId) const {
    const std::map<TrainId, GroupRun>::const_iterator it = groups_.find(trainId);
    return it == groups_.end() ? rs::world::NilGuid() : it->second.visitPlatform;
}

bool DiaRuntime::GroupDepartureAbsSeconds(TrainId trainId, double& outAbsSeconds) const {
    const std::map<TrainId, GroupRun>::const_iterator it = groups_.find(trainId);
    if (it == groups_.end() || !it->second.departureScheduled) return false;
    outAbsSeconds = it->second.departureAbsSeconds;
    return true;
}

int DiaRuntime::PlatformRoutineCursorIndex(
    const rs::world::Guid& platformId,
    const std::vector<rs::world::PlatformRoutineElement>& routine, int layerKey,
    TrainId trainId) const {
    if (!rs::world::GuidIsNil(trainId)) {
        const std::unordered_map<rs::world::Guid,
                                 std::unordered_map<TrainId, int, rs::world::GuidHash>,
                                 rs::world::GuidHash>::const_iterator trainIterator =
            trainRoutineCursor_.find(platformId);
        if (trainIterator == trainRoutineCursor_.end()) {
            return rs::world::ResolveNextRoutineIndex(routine, 0);
        }
        const std::unordered_map<TrainId, int, rs::world::GuidHash>::const_iterator cursorIterator =
            trainIterator->second.find(trainId);
        const int cursor = cursorIterator == trainIterator->second.end() ? 0 : cursorIterator->second;
        return rs::world::ResolveNextRoutineIndex(routine, cursor);
    }
    const std::unordered_map<rs::world::Guid, std::unordered_map<int, int>, rs::world::GuidHash>::const_iterator iterator =
        routineCursor_.find(platformId);
    if (iterator == routineCursor_.end()) {
        return rs::world::ResolveNextRoutineIndex(routine, 0);
    }
    const std::unordered_map<int, int>::const_iterator layerIterator = iterator->second.find(layerKey);
    const int cursor = layerIterator == iterator->second.end() ? 0 : layerIterator->second;
    return rs::world::ResolveNextRoutineIndex(routine, cursor);
}

const rs::world::PlatformRoutineElement* DiaRuntime::PeekPlatformRoutine(
    const rs::world::PlatformStopInfo& platform, TrainId trainId) const {
    const rs::world::ResolvedPlatformRoutine resolved = ResolvePlatformRoutine(platform, trainId);
    if (resolved.routine == nullptr) return nullptr;
    return rs::world::PeekRoutineElement(*resolved.routine,
                                         ReadPlatformRoutineCursor(platform, resolved));
}

void DiaRuntime::ConsumePlatformRoutine(const rs::world::PlatformStopInfo& platform, TrainId trainId) {
    const rs::world::ResolvedPlatformRoutine resolved = ResolvePlatformRoutine(platform, trainId);
    if (resolved.routine == nullptr) return;
    int& cursor = WritePlatformRoutineCursor(platform, resolved);
    (void)rs::world::DequeueRoutineElement(*resolved.routine, cursor);
}

rs::world::ResolvedPlatformRoutine DiaRuntime::ResolvePlatformRoutine(
    const rs::world::PlatformStopInfo& platform, TrainId trainId) const {
    return rs::world::ResolveTrainRoutine(platform.trainRoutines, platform.formationRoutines,
                                          platform.tagRoutines, platform.routine, trainId,
                                          tagRegistry_.FormationOfTrain(trainId),
                                          tagRegistry_.TrainTags(trainId));
}

int DiaRuntime::ReadPlatformRoutineCursor(
    const rs::world::PlatformStopInfo& platform,
    const rs::world::ResolvedPlatformRoutine& resolved) const {
    if (!rs::world::GuidIsNil(resolved.trainId)) {
        const std::unordered_map<rs::world::Guid,
                                 std::unordered_map<TrainId, int, rs::world::GuidHash>,
                                 rs::world::GuidHash>::const_iterator trainIterator =
            trainRoutineCursor_.find(platform.id);
        if (trainIterator == trainRoutineCursor_.end()) {
            return 0;
        }
        const std::unordered_map<TrainId, int, rs::world::GuidHash>::const_iterator cursorIterator =
            trainIterator->second.find(resolved.trainId);
        const int cursor = cursorIterator == trainIterator->second.end() ? 0 : cursorIterator->second;
        return cursor;
    }

    const std::unordered_map<rs::world::Guid, std::unordered_map<int, int>, rs::world::GuidHash>::const_iterator
        iterator = routineCursor_.find(platform.id);
    if (iterator == routineCursor_.end()) {
        return 0;
    }
    const std::unordered_map<int, int>::const_iterator layerIterator =
        iterator->second.find(resolved.layerKey);
    const int cursor = layerIterator == iterator->second.end() ? 0 : layerIterator->second;
    return cursor;
}

int& DiaRuntime::WritePlatformRoutineCursor(
    const rs::world::PlatformStopInfo& platform,
    const rs::world::ResolvedPlatformRoutine& resolved) {
    if (!rs::world::GuidIsNil(resolved.trainId)) {
        return trainRoutineCursor_[platform.id][resolved.trainId];
    }
    return routineCursor_[platform.id][resolved.layerKey];
}

void DiaRuntime::RemoveGroup(TrainId trainId) {
    groups_.erase(trainId);
    tagRegistry_.RemoveTrain(trainId);
    runInfoRegistry_.RemoveTrain(trainId);
    for (auto& [platformId, trainCursors] : trainRoutineCursor_) {
        (void)platformId;
        trainCursors.erase(trainId);
    }
}

DiaRuntimeSnapshot DiaRuntime::Snapshot() const {
    DiaRuntimeSnapshot snapshot;
    snapshot.routineCursors = routineCursor_;
    snapshot.trainRoutineCursors = trainRoutineCursor_;
    snapshot.groups.reserve(groups_.size());
    for (const std::pair<const TrainId, GroupRun>& entry : groups_) {
        const GroupRun& run = entry.second;
        snapshot.groups.push_back({ entry.first, run.state, run.visitPlatform, run.departedPlatform,
                                    run.departureScheduled, run.departureAbsSeconds,
                                    run.doorCloseAbsSeconds, run.phaseElapsed, run.currentAction,
                                    run.currentSwitchChanges });
    }
    return snapshot;
}

void DiaRuntime::Restore(const DiaRuntimeSnapshot& snapshot) {
    groups_.clear();
    routineCursor_ = snapshot.routineCursors;
    trainRoutineCursor_ = snapshot.trainRoutineCursors;
    for (const DiaRuntimeGroupSnapshot& saved : snapshot.groups) {
        if (rs::world::GuidIsNil(saved.trainId)) continue;
        GroupRun run;
        run.state = saved.state;
        run.visitPlatform = saved.visitPlatform;
        run.departedPlatform = saved.departedPlatform;
        run.departureScheduled = saved.departureScheduled;
        run.departureAbsSeconds = saved.departureAbsSeconds;
        run.doorCloseAbsSeconds = saved.doorCloseAbsSeconds;
        run.phaseElapsed = saved.phaseElapsed;
        run.currentAction = saved.currentAction;
        run.currentSwitchChanges = saved.currentSwitchChanges;
        groups_.emplace(saved.trainId, std::move(run));
    }
}

void DiaRuntime::Clear() {
    groups_.clear();
    routineCursor_.clear();
    trainRoutineCursor_.clear();
}

} // namespace rs::sim

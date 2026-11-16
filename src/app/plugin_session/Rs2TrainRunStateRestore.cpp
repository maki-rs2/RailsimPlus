#include "Rs2TrainRunStateRestore.h"

#include "../../core/SpeedUnits.h"
#include "../../plugins/layout/Rs2DiaImport.h"

namespace rs::app {

namespace {

constexpr int kStateBraking = 1;
constexpr int kStateStopped = 2;
constexpr int kStateDeparting = 3;

}  // namespace

bool RunStateOpensAnyDoor(const rs::legacy_layout::Rs2TrainGroupRunState& runState) {
    return runState.openDoor[0] || runState.openDoor[1];
}

bool IsStoppedRunState(const rs::legacy_layout::Rs2TrainGroupRunState& runState) {
    return runState.present &&
           (runState.state == kStateStopped || runState.state == kStateDeparting);
}

std::optional<Rs2TrainRestore> BuildRs2TrainRestore(
    const rs::legacy_layout::Rs2TrainGroupRunState& runState, rs::world::TrainId trainId,
    size_t sceneIndex, const rs::world::PlatformStopInfo* platform, double nowAbsSeconds,
    double doorCloseLeadSeconds,
    const std::string& groupName, std::vector<std::string>& warnings) {
    if (!runState.present) return std::nullopt;
    if (runState.state == kStateBraking) {
        warnings.push_back("TrainGroup「" + groupName +
                           "」は停車目標へ減速中(State=1)でしたが、走行状態として始めます");
        return std::nullopt;
    }
    if (!IsStoppedRunState(runState)) return std::nullopt;

    rs::world::Guid platformId = rs::world::NilGuid();
    if (platform == nullptr) {
        warnings.push_back("TrainGroup「" + groupName +
                           "」は停車中でしたが、足元に停車できる番線が見つかりません。番線なしの停車として始めます");
    } else {
        platformId = platform->id;
        // ドアを開ける指定は、番線の指定から減速開始時に入る。食い違うなら番線の特定を疑う。
        if (RunStateOpensAnyDoor(runState) != platform->OpensAnyDoor()) {
            warnings.push_back("TrainGroup「" + groupName +
                               "」のドア開閉の保存値と、足元の番線「" + platform->name +
                               "」のOpenDoorが食い違っています");
        }
    }

    rs::sim::DiaStopSpec spec;
    if (runState.dia.present) {
        spec = rs::sim::DiaRuntime::ApplyRoutineElement(
            spec, rs::legacy_layout::ConvertDiaElement(runState.dia));
    }

    Rs2TrainRestore restore;
    restore.train.trainId = trainId;
    restore.train.sceneIndex = sceneIndex;
    // started=falseのままにして、最初のtickが先頭位置と占有edge列を作り直す。
    restore.train.kin.started = false;
    restore.train.kin.targetSpeed = rs::core::KilometersPerHourToMetersPerSecond(runState.targetSpeed);
    restore.train.kin.stopDistance = 0.0;
    restore.train.kin.arrivedAtStop = true;
    restore.train.lastTravelReversed = runState.reverse;
    restore.train.stopTargetReversed = runState.targetSpeed < 0.0;
    restore.train.targetPlatform = platformId;

    restore.dia = rs::sim::DiaRuntime::MakeArrivedSnapshot(
        trainId,
        runState.state == kStateDeparting ? rs::sim::DiaState::Departing : rs::sim::DiaState::Stopped,
        platformId, spec, nowAbsSeconds, doorCloseLeadSeconds);
    return restore;
}

}  // namespace rs::app

// 編成1つぶんのtick処理(先読み範囲の解決・ダイヤ・速度上限・キネマティクス・車両姿勢・ドア・
// 車両スロットのスナップショット更新)。SceneTrainController::Tickが編成ごとに呼ぶ。
#include "SceneTrainController.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_set>
#include <utility>

#include "TrainDoorSide.h"
#include "SceneStopConstraintPhase.h"
#include "SceneRouteReservationPhase.h"
#include "SceneTrainFade.h"
#include "../../world/alignment/TrackForwardScan.h"
#include "../../world/scene/ScenePlacement.h"
#include "../../sim/SimTuning.h"

namespace rs::app {
namespace {
// 駅停車目標は設定済みの間は再探索しないため、発車直後も次の停車目標を見失わない距離を確保する。
constexpr int kThrottleIntervalTicks = 4;
} // namespace

SceneTrainController::GroupForwardScan SceneTrainController::ResolveGroupForwardSpan(
    const TrainGroupSnapshot& group,
    const std::function<rs::world::TrackGraph*(size_t)>& graphForScene,
    const std::function<const rs::world::TrackGraph*(size_t)>& constGraphForScene,
    const rs::world::WarpLinkTable& warpTable,
    const std::unordered_set<rs::world::Guid, rs::world::GuidHash>& occupiedBlocks,
    const std::unordered_set<rs::world::Guid, rs::world::GuidHash>& previousOccupiedBlocks,
    double nowAbsSeconds) {
    const TrainId trainId = group.trainId;
    const double speedKmh = std::abs(runtime_.GroupCurrentSpeedKmh(trainId));
    const int throttleSlot = static_cast<int>(rs::world::GuidHash{}(trainId) %
                                              kThrottleIntervalTicks);
    const bool throttleTick = throttleSlot ==
        static_cast<int>(tickCounter_ % static_cast<uint64_t>(kThrottleIntervalTicks));
    std::unordered_map<TrainId, CachedForwardConstraints, rs::world::GuidHash>::iterator
        cachedConstraints = cachedForwardConstraints_.find(trainId);
    const rs::sim::SeekAheadTuning& seekAhead = rs::sim::SimTuning::Embedded().seekAhead;
    GroupForwardScan result;
    result.groupMoving = speedKmh >= seekAhead.stoppedVelocityEpsilonKmh;
    result.runFullSearch = result.groupMoving || throttleTick ||
                           cachedConstraints == cachedForwardConstraints_.end();
    const double brakingM = rs::sim::ApproachBrakingDistanceM(
        speedKmh, group.maxDecelerationKmhPerS, rs::sim::SimTuning::Embedded().approach);
    result.dynamicSeekAheadM = std::clamp(
        brakingM + seekAhead.marginM, seekAhead.minM, seekAhead.maxM);
    if (result.runFullSearch) {
        rs::sim::LeadingEnd leadingEnd;
        runtime_.GetGroupForwardScanStart(constGraphForScene, warpTable, trainId, leadingEnd);
        // seek集合: 遠方span構築より前に、通りがかる
        // 未決定の分岐をその場で解決し固定する。物理通過時(TrainRuntime::TickGroup→
        // ApplyPointRoutinesAheadAcrossScenes)は既に決定済みのものを再Dequeueしない
        // ため、ここで決めた経路がそのまま実走行に使われる。
        runtime_.ResolveGroupSeekAheadPointRoutines(
            graphForScene, warpTable, trainId, dia_.TagRegistry().FormationOfTrain(trainId),
            leadingEnd.sceneIndex, leadingEnd.position,
            result.dynamicSeekAheadM, occupiedBlocks, dia_.TagRegistry().TrainTags(trainId),
            nowAbsSeconds, previousOccupiedBlocks);
        // 直前に解決した列車ごとの分岐決定(共有activeSlotへは未反映)を、このtickの
        // 以降の全ての読み取り専用先読みへ一貫して見せる(複数列車が同じ未決定の分岐へ
        // 先読みで到達しても、互いの決定を上書きしないようにするため)。
        const rs::world::NodeSlotOverride* slotOverride = runtime_.GroupSeekDecisions(trainId);
        CachedForwardConstraints& cached = cachedForwardConstraints_[trainId];
        cached.forwardSpan = rs::world::ScanForwardSpanAcrossScenes(
            constGraphForScene, warpTable, leadingEnd.sceneIndex, leadingEnd.position,
            result.dynamicSeekAheadM, slotOverride);
        result.forwardSpan = &cached.forwardSpan;
        // 読込直後、この編成の車両がまだGPU常駐スロットもキャッシュ済みテンプレートも
        // 持たない一瞬だけtrainLengthM<=0になりうる(SceneTrainGroupCollector.cpp、
        // 非常駐車両を積算対象から除外する既知の限界)。その状態でSeekStationStopを
        // 呼ぶと、編成長0のまま停止距離をラッチしてしまい(番線長の半分だけを基準にした
        // 位置になり本来の中央基準からずれる)、次に停止目標を破棄するまで(到着/発車まで)
        // 固定され続ける。trainLengthMが有効になるまで探索自体を見送る。
        if (group.trainLengthM > 0.0) {
            runtime_.SeekStationStop(constGraphForScene, warpTable, trainId,
                                     result.dynamicSeekAheadM, group.trainLengthM, result.forwardSpan,
                                     [this, trainId](const rs::world::PlatformStopInfo& platform) {
                                         return dia_.PeekPlatformRoutine(platform, trainId);
                                     }, [this, trainId](const rs::world::PlatformStopInfo& platform) {
                                         dia_.ConsumePlatformRoutine(platform, trainId);
                                     }, slotOverride);
        }
        // 駅の停止目標が決まった後で、前方のポイントを予約する。
        deniedSignals_[trainId] = RunRouteReservationPhase(
            routeReservation_, runtime_, graphForScene, warpTable, trainId, cached.forwardSpan,
            result.dynamicSeekAheadM, slotOverride, occupiedBlocks, group.trainLengthM,
            nowAbsSeconds);
    } else {
        result.forwardSpan = &cachedConstraints->second.forwardSpan;
        result.effectiveMaxVelocityKmh = cachedConstraints->second.effectiveMaxVelocityKmh;
    }
    return result;
}

rs::sim::DiaTickResult SceneTrainController::TickGroupDia(
    const TrainGroupSnapshot& group, const rs::world::TrackGraph& trainGraph,
    double nowAbsSeconds, double timeScale) {
    const TrainId trainId = group.trainId;
            // ダイヤ状態機械はキネマティクスより前に回す。
            // 発車が決まったtickでは、この後のTickGroupが動き出す前に自動停止目標を
            // 外しておく必要があるため(外すだけで編成は元のtargetSpeedへ再加速する)。
            rs::sim::DiaTickInput diaInput;
    diaInput.hasStopTarget = runtime_.GroupHasStopTarget(trainId);
    diaInput.arrivedAtStop = runtime_.GroupArrivedAtStop(trainId);
    diaInput.targetPlatform = runtime_.GroupTargetPlatform(trainId);
    if (diaInput.targetPlatform != rs::world::NilGuid()) {
        for (const rs::world::PlatformStopInfo& platform : trainGraph.platforms) {
            if (platform.id == diaInput.targetPlatform) {
                diaInput.targetPlatformInfo = &platform;
                break;
            }
        }
    }
    // 到着したtickの値だけが使われる。番線を伴わない停止ではドアを開けないので0になる。
    diaInput.doorCloseLeadSeconds = rs::sim::DiaRuntime::DoorCloseLeadSeconds(
        group.doorClosingTimeSeconds,
        diaInput.targetPlatformInfo != nullptr && diaInput.targetPlatformInfo->OpensAnyDoor(),
        timeScale);
    // HUDの前駅・次駅・番線表示用。diaInput解決と同じタイミングでキャッシュし、クエリ時
    // (GroupStopLabels)にgraphアクセスを要らなくする。対象なしなら空文字列(HUD側は項目ごと非表示にする)。
    // 次駅が変わったら、直前の次駅を前駅へ送る(対象なしを挟んでも前駅は残す)。
    StopLabels& labels = lastStopLabels_[trainId];
    std::string nextStation;
    std::string nextPlatform;
    if (diaInput.targetPlatformInfo != nullptr) {
        nextPlatform = diaInput.targetPlatformInfo->name;
        for (const rs::world::StationInfo& station : trainGraph.stations) {
            if (station.id == diaInput.targetPlatformInfo->stationId) {
                nextStation = station.name;
                break;
            }
        }
    }
    if (!nextStation.empty() && nextStation != labels.lastTargetStation) {
        if (!labels.lastTargetStation.empty()) labels.previousStation = labels.lastTargetStation;
        labels.lastTargetStation = nextStation;
    }
    labels.nextStation = nextStation;
    labels.nextPlatform = nextPlatform;
    rs::sim::DiaTickResult diaResult = dia_.Tick(trainId, diaInput, nowAbsSeconds);
    const std::unordered_map<TrainId, rs::world::Guid, rs::world::GuidHash>::iterator lastTarget =
        lastKnownTargetPlatform_.find(trainId);
    const bool targetChanged = lastTarget == lastKnownTargetPlatform_.end() ||
        lastTarget->second != diaInput.targetPlatform;
    if (targetChanged && diaInput.targetPlatform != rs::world::NilGuid()) {
        const rs::world::TrainRunInfo* runInfo = dia_.RunInfoRegistry().RunInfo(trainId);
        if (runInfo != nullptr) {
            stationRunInfoDispatcher_.MarkPlatformLocked(diaInput.targetPlatform, *runInfo);
        }
    }
    lastKnownTargetPlatform_[trainId] = diaInput.targetPlatform;
    if (diaResult.runInfoJustApplied) {
        const rs::world::TrainRunInfo* runInfo = dia_.RunInfoRegistry().RunInfo(trainId);
        if (runInfo != nullptr) {
            stationRunInfoDispatcher_.MarkPlatformArrived(diaInput.targetPlatform, *runInfo);
        }
    }
    if (diaResult.departNow) {
        // Action=Reverse(折返)は、headの張替えではなく
        // 目標速度の符号を反転させるだけで表現する(既存の負速度キネマティクス
        // で足りる)。ClearGroupStopTargetより前に反転させる
        // 必要はない(targetSpeed自体はClearGroupStopTargetで書き換わらない)。
        if (diaResult.departAction == rs::world::DiaAction::Reverse) {
            runtime_.SetGroupTargetSpeed(trainId, -runtime_.GroupTargetSpeedKmh(trainId));
        }
        if (diaInput.targetPlatform != rs::world::NilGuid()) {
            stationRunInfoDispatcher_.MarkPlatformDeparted(diaInput.targetPlatform);
        }
        runtime_.ClearGroupStopTarget(trainId);
    }
    return diaResult;
}

double SceneTrainController::ComputeGroupEffectiveMaxVelocity(
    const TrainGroupSnapshot& group,
    const std::function<const rs::world::TrackGraph*(size_t)>& constGraphForScene,
    const rs::world::WarpLinkTable& warpTable, const GroupForwardScan& forwardScan,
    const std::vector<rs::sim::OccupancyTrain>& occupancyTrains,
    const std::unordered_set<rs::world::Guid, rs::world::GuidHash>& blocksOccupiedByOthers,
    double dt) {
    const TrainId trainId = group.trainId;
    const rs::world::NodeSlotOverride* slotOverride = runtime_.GroupSeekDecisions(trainId);
    const rs::sim::SpeedLimitLookahead speedLimits = runtime_.ForwardSpeedLimits(
        constGraphForScene, warpTable, trainId, forwardScan.dynamicSeekAheadM,
        forwardScan.forwardSpan, slotOverride,
        rs::sim::MakeSignalSpeedForTrainFn(signalAspectRuntime_, trainId,
                                           deniedSignals_.contains(trainId)
                                               ? &deniedSignals_.at(trainId)
                                               : nullptr));
    // 前方で下がる制限は、停止と同じ制動パターンで手前から減速する制約として渡す。
    runtime_.SetGroupSpeedLimitConstraints(trainId, speedLimits.ahead);
    // Currentはフェーズ①の移動前位置なので、全グループで同一tickの入口状態を共有する。
    RunStopConstraintPhase(constGraphForScene, warpTable, trainId, runtime_, occupancyTrains,
                           blocksOccupiedByOthers, forwardScan.dynamicSeekAheadM,
                           forwardScan.forwardSpan, slotOverride, dt);
    // 信号の速度制限は、この列車に適用される現示の速度として閉塞のedge区間の速度制限へ重なっている。
    const double effectiveMaxVelocityKmh =
        std::min(group.maxVelocityKmh, speedLimits.inSectionCapKmh);
    cachedForwardConstraints_[trainId].effectiveMaxVelocityKmh = effectiveMaxVelocityKmh;
    return effectiveMaxVelocityKmh;
}

void SceneTrainController::TickGroupKinematics(const TrainGroupSnapshot& group,
                                                const KinematicsInput& input,
                                                KinematicsResult& result) {
    rs::world::SceneRegistry& registry = *input.registry;
    runtime_.SetGroupOccupiedPathKeepLength(group.trainId, group.occupiedPathKeepLengthM);
    const bool running = runtime_.TickGroup(
        *input.graphForScene, *input.warpTable, result.trainSceneIndex, input.dt, group.trainId,
        group.leadPlacement->position, input.effectiveMaxVelocityKmh, group.maxAccelerationKmhPerS,
        group.maxDecelerationKmhPerS, result.head, result.dsMeters, result.trainSceneIndex,
        *input.blocksOccupiedByOthers, dia_.TagRegistry().TrainTags(group.trainId),
        dia_.TagRegistry().FormationOfTrain(group.trainId),
        input.nowAbsSeconds, input.signalPhase->previousOccupiedBlockGuids, input.forwardSpan);
    if (running) {
        rs::world::TrackPlacement updated = *group.leadPlacement;
        updated.position = result.head;
        registry.UpdatePlacementWithScene(group.leadEntity->guid, updated, result.trainSceneIndex);
    }
}

std::vector<rs::legacy_plugin::CarPose> SceneTrainController::SolveGroupCarPoses(
    const TrainGroupSnapshot& group, const rs::world::SceneRegistry& registry,
    const std::vector<SceneSlotPool::Slot>& slots,
    const std::function<const rs::world::TrackGraph*(size_t)>& constGraphForScene,
    const rs::world::WarpLinkTable& warpTable, size_t trainSceneIndex,
    const rs::world::TrackPosition& head) const {
    std::vector<rs::legacy_plugin::CarSpec> specs;
    specs.reserve(group.slotIndices.size());
    for (size_t slotIndex : group.slotIndices) {
        const rs::world::SceneEntity* entity = registry.FindEntity(slots[slotIndex].entityId);
        rs::legacy_plugin::CarSpec spec;
        spec.frontLimit = slots[slotIndex].carSnapshot.trainTemplate.vehicle.frontLimit;
        spec.tailLimit = slots[slotIndex].carSnapshot.trainTemplate.vehicle.tailLimit;
        spec.reversed = entity && entity->reversed;
        specs.push_back(spec);
    }
    return rs::legacy_plugin::SolveTrainHeadsAcrossScenes(
        constGraphForScene, warpTable, trainSceneIndex, head, specs,
        runtime_.GroupLastTravelReversed(group.trainId), runtime_.GroupOccupiedPath(group.trainId));
}

SceneTrainController::GroupDoorBase SceneTrainController::ResolveGroupDoorBase(
    TrainId trainId, const rs::world::TrackGraph* trainGraph,
    const rs::sim::DiaTickResult& diaResult,
    const std::vector<rs::legacy_plugin::CarPose>& poses) const {
    GroupDoorBase result;
        // 停車中の番線はダイヤ状態機械が持つ(TrainRuntimeのtargetPlatformは発車後も
        // ラッチされたまま残るため、発車後もドアが開いたままになる)。
        if (trainGraph && diaResult.doorsOpen) {
        const rs::world::Guid targetPlatformId = dia_.GroupVisitPlatform(trainId);
        for (const rs::world::PlatformStopInfo& platform : trainGraph->platforms) {
            if (platform.id == targetPlatformId) {
                result.base1 = platform.openDoor[1];
                result.base2 = platform.openDoor[0];
                if (!poses.empty()) {
                    result.sideReference =
                        ResolveDoorSideReference(*trainGraph, platform, poses.front().head);
                }
                break;
            }
        }
    }
    for (const rs::world::SwitchChange& change : diaResult.activeSwitchChanges) {
        if (change.switchName == "_DOOR1") result.base1 = change.value != 0;
        if (change.switchName == "_DOOR2") result.base2 = change.value != 0;
    }
    return result;
}

bool SceneTrainController::UpdateCarSlotSnapshot(
    SceneSlotPool::Slot& slot, const rs::world::SceneEntity* entity,
    const rs::legacy_plugin::CarPose& pose, size_t index, size_t carCount,
    TrainId trainId, const rs::sim::DiaTickResult& diaResult,
    const GroupDoorBase& doorBase, const rs::world::TrackGraph& activeGraph,
    size_t activeSceneIndex,
    const std::function<const rs::world::TrackGraph*(size_t)>& constGraphForScene,
    const rs::world::WarpLinkTable& warpTable, double dt, double dsMeters, bool groupMoving,
    const std::optional<rs::world::Guid>& cabinLeadCarEntityId) {
    const bool reversed = entity && entity->reversed;
    UpdateTrainWarpFade(slot, pose.fadeAlpha, pose.spansScenes, dsMeters, groupMoving);
            // 描画可否: 車両headのいるSceneがアクティブで、かつ全車軸のchaseが
            // アクティブScene内で完結すること。spansScenes(連結限界ベースの跨ぎ
            // 判定)だけでは、SolveAxlesが実際にchaseする車軸範囲が収まる確認に
            // ならず、端点nodeへ車軸が張り付いた(軸間の詰まった)姿勢のまま描いて
            // しまう。dead-end(車止め)で行き止まる場合はScene跨ぎではないので
            // 描く(判定をatEndではなくScene一致で行う理由)。
            bool drawable = pose.sceneIndex == activeSceneIndex && !pose.spansScenes;
    rs::world::WarpAdvanceResult axleTail;
    if (drawable) {
        const double maxOffset = MaxAxleChaseOffset(slot.carSnapshot.trainTemplate, reversed);
        axleTail = rs::world::ChaseBackwardAcrossScenes(
            constGraphForScene, warpTable, pose.sceneIndex, pose.head, maxOffset);
        drawable = axleTail.sceneIndex == pose.sceneIndex && axleTail.warpHops == 0 &&
                   !axleTail.truncatedAtWarp;
    }
    if (drawable && !slot.carSnapshot.carVisible) slot.carSnapshot.tiltState.valid = false;
    slot.carSnapshot.carVisible = drawable;
    if (!drawable) {
        slot.carSnapshot.carSnapshotValid = false;
        slot.carSnapshot.carSnapshotDeltaSeconds = 0.0;
        slot.carSnapshot.carSnapshotDistanceMeters = 0.0;
        return false;
    }
    rs::world::TrackPlacement highlightPlacement;
    highlightPlacement.position = pose.head;
    highlightPlacement.yawOffsetDeg = reversed ? 180.0f : 0.0f;
    slot.carSnapshot.lastCarWorld = rs::world::ResolveWorldMatrix(highlightPlacement, activeGraph);
    slot.carSnapshot.carSnapshotHead = pose.head;
    slot.carSnapshot.carSnapshotReversed = reversed;
    slot.carSnapshot.carSnapshotPathOffsetM = pose.pathOffsetM;
    slot.carSnapshot.carSnapshotConnectFront = reversed ? index + 1 < carCount : index > 0;
    slot.carSnapshot.carSnapshotConnectRear = reversed ? index > 0 : index + 1 < carCount;
    std::map<std::string, int32_t> routineSwitchOverrides;
            // ドアの左右はRailSim IIでは2段の反転の合成になる(CTrainGroup.cpp):
            // 停車時に platform の左右を編成ローカルへ写す反転(進行方向 vs レール方向)と、
            // 編成ローカルを車両の_DOOR1/_DOOR2へ写す反転(編成の向き vs 車両の向き)。
            // 2つを合成すると走行方向の項が相殺し、「車体がedgeのどちら向きを向いて
            // いるか」だけが残る。つまり開く側は物理的な側であり、前進/後退では
            // 入れ替わらない。
            // 基準は番線側で1回決め、各車両へはワールド右手方向の一致で写す。
            // 編成内の全車両が同じ物理側を開くことが、この構成の不変条件である。
            const bool doorSideFlipped =
        IsDoorSideFlipped(activeGraph, doorBase.sideReference, pose.head, reversed);
    routineSwitchOverrides["_DOOR1"] =
        (doorSideFlipped ? doorBase.base2 : doorBase.base1) ? 1 : 0;
    routineSwitchOverrides["_DOOR2"] =
        (doorSideFlipped ? doorBase.base1 : doorBase.base2) ? 1 : 0;
    // RailSim IIのCModelSwitch.cpp:559/CTrainGroup.cpp:780。運転台視点の対象車両
    // (先頭/後尾どちらか1両のみ)だけが1、同一編成の他車両・非アクティブなら0。
    routineSwitchOverrides["_CABINVIEW"] =
        (cabinLeadCarEntityId.has_value() && entity != nullptr &&
         entity->guid == *cabinLeadCarEntityId)
            ? 1
            : 0;
    for (const rs::world::SwitchChange& change : diaResult.activeSwitchChanges) {
        if (change.switchName != "_DOOR1" && change.switchName != "_DOOR2") {
            routineSwitchOverrides[change.switchName] = change.value;
        }
    }
    slot.carSnapshot.carSnapshotRoutineSwitchOverrides = std::move(routineSwitchOverrides);
    slot.carSnapshot.carSnapshotTrainId = trainId;
    slot.carSnapshot.carSnapshotDeltaSeconds += dt;
    slot.carSnapshot.carSnapshotDistanceMeters += dsMeters;
    slot.carSnapshot.carSnapshotValid = true;
    return true;
}

} // namespace rs::app

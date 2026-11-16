// 編成tickの全体の流れ(準備・編成ごとの処理・後始末)。編成1つぶんの処理は
// SceneTrainControllerGroupTick.cpp、問い合わせ・設定の窓口はSceneTrainControllerQueries.cpp。
#include "SceneTrainController.h"

#include "../../platform/EnvironmentVariable.h"
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <unordered_set>
#include <utility>

#include <spdlog/spdlog.h>

#include "SceneTrainFade.h"
#include "../../world/alignment/TrackForwardScan.h"
#include "../../sim/SimTuning.h"

namespace rs::app {
namespace {
// RunSignalBlockPhaseの各scene走査ループはgraphForScene(sceneIndex)がnullptrを返した
// 時点で打ち切るため、実際のscene数はgraphForScene側が規定する。ここは走査回数の
// 形式的な上限にすぎず、実データを人為的に切り詰めない値(size_tの最大)を渡す
// (Scene数は原則無制限)。
constexpr size_t kUnboundedScanSceneCount = std::numeric_limits<size_t>::max();

using ProfileClock = std::chrono::steady_clock;

double ToMilliseconds(std::chrono::nanoseconds duration) {
    return std::chrono::duration<double, std::milli>(duration).count();
}
} // namespace

void SceneTrainController::ResetWarpFade(std::vector<SceneSlotPool::Slot>& slots) {
    for (Slot& slot : slots) ResetTrainWarpFade(slot);
    lastActiveSceneIndex_.reset();
}

void SceneTrainController::RemapSceneIndices(const std::vector<size_t>& oldToNew) {
    runtime_.RemapSceneIndices(oldToNew);
    routeReservation_.RemapSceneIndices(oldToNew);
    cachedForwardConstraints_.clear();
}

std::vector<rs::sim::OccupancyTrain> SceneTrainController::BuildOccupancyTrains(
    const std::vector<TrainGroupSnapshot>& groups,
    const std::function<rs::world::TrackGraph*(size_t)>& graphForScene) const {
    std::vector<rs::sim::OccupancyTrain> occupancyTrains;
    for (const TrainGroupSnapshot& group : groups) {
        if (group.leadPlacement == nullptr) continue;
        occupancyTrains.push_back(
            {group.trainId, group.leadPlacement->position, group.trainLengthM,
             group.leadEntity->placementSceneIndex, runtime_.GroupOccupiedPath(group.trainId),
             [&graphForScene](size_t sceneIndex) -> const rs::world::TrackGraph* {
                 return graphForScene(sceneIndex);
             },
             runtime_.GroupCurrentSpeedKmh(group.trainId)});
    }
    return occupancyTrains;
}

void SceneTrainController::TickGroupOnGraph(
    const TrainGroupSnapshot& group, const rs::world::TrackGraph& trainGraph,
    const TickFrame& frame, TickAccumulators& acc, GroupMotion& motion) {
    const ProfileClock::time_point searchStart = ProfileClock::now();
    // ポイント在線ガードは、条件レイヤ(前tickスナップショット)
    // とは異なり、フェーズ①で全編成の移動前位置から作ったCurrentを参照する。
    // フェーズ④のこのループの処理順によらず、同一tickの入口状態を全編成で
    // 共有するためである。自編成の在線ではガードしない。
    const std::unordered_set<rs::world::Guid, rs::world::GuidHash> blocksOccupiedByOthers =
        rs::sim::BlocksOccupiedByOtherTrains(blockOccupancyTracker_.Current(), group.trainId);
    const GroupForwardScan forwardScan = ResolveGroupForwardSpan(
        group, *frame.graphForScene, *frame.constGraphForScene, *frame.warpTable,
        blocksOccupiedByOthers, frame.signalPhase->previousOccupiedBlockGuids,
        frame.nowAbsSeconds);
    motion.groupMoving = forwardScan.groupMoving;
    motion.diaResult = TickGroupDia(group, trainGraph, frame.nowAbsSeconds, frame.timeScale);
    double effectiveMaxVelocityKmh = forwardScan.effectiveMaxVelocityKmh;
    if (forwardScan.runFullSearch) {
        effectiveMaxVelocityKmh = ComputeGroupEffectiveMaxVelocity(
            group, *frame.constGraphForScene, *frame.warpTable, forwardScan,
            *frame.occupancyTrains, blocksOccupiedByOthers, frame.dt);
        acc.searchTime += ProfileClock::now() - searchStart;
    }
    const ProfileClock::time_point kinematicsStart = ProfileClock::now();
    KinematicsInput kinematicsInput;
    kinematicsInput.registry = frame.registry;
    kinematicsInput.graphForScene = frame.graphForScene;
    kinematicsInput.warpTable = frame.warpTable;
    kinematicsInput.dt = frame.dt;
    kinematicsInput.nowAbsSeconds = frame.nowAbsSeconds;
    kinematicsInput.slots = frame.slots;
    kinematicsInput.signalPhase = frame.signalPhase;
    kinematicsInput.blocksOccupiedByOthers = &blocksOccupiedByOthers;
    kinematicsInput.effectiveMaxVelocityKmh = effectiveMaxVelocityKmh;
    kinematicsInput.forwardSpan = forwardScan.forwardSpan;
    KinematicsResult kinematicsResult;
    kinematicsResult.head = motion.head;
    kinematicsResult.dsMeters = motion.dsMeters;
    kinematicsResult.trainSceneIndex = motion.trainSceneIndex;
    TickGroupKinematics(group, kinematicsInput, kinematicsResult);
    motion.head = kinematicsResult.head;
    motion.dsMeters = kinematicsResult.dsMeters;
    motion.trainSceneIndex = kinematicsResult.trainSceneIndex;
    acc.kinematicsTime += ProfileClock::now() - kinematicsStart;
    // 駅舎の接近判定。
    // 起点は物理的な先端。TickGroup後の位置で求める。
    rs::sim::ApproachTrain approachTrain;
    if (runtime_.GetGroupForwardScanStart(*frame.constGraphForScene, *frame.warpTable,
                                          group.trainId, approachTrain.front)) {
        approachTrain.speedKmh = std::fabs(runtime_.GroupCurrentSpeedKmh(group.trainId));
        approachTrain.maxDecelerationKmhPerS = group.maxDecelerationKmhPerS;
        approachTrain.trainLengthM = group.trainLengthM;
        // RailSim IIの m_State==2(停止してドアを開けている状態)そのもの。
        approachTrain.stopping = motion.diaResult.state == rs::sim::DiaState::Stopped;
        approachTrain.stopDistanceM = runtime_.GroupStopDistanceM(group.trainId);
        approachTrain.slotOverride = runtime_.GroupSeekDecisions(group.trainId);
        acc.approachTrains.push_back(approachTrain);
        // 共有spanはTickGroup前の先端が起点なので1tick古い。接近判定の下限距離は
        // 50mであり、1tickの移動量はサブメートルなので許容する。
        // approachTrainsと添字を揃えて積む(接近判定側が同じ位置で引く)。
        acc.approachForwardSpans.push_back(*forwardScan.forwardSpan);
    }
}

void SceneTrainController::UpdateGroupCarSlots(
    const TrainGroupSnapshot& group, const rs::world::TrackGraph* trainGraph,
    const TickFrame& frame, const GroupMotion& motion, TickAccumulators& acc) {
    const std::vector<rs::legacy_plugin::CarPose> poses = SolveGroupCarPoses(
        group, *frame.registry, *frame.slots, *frame.constGraphForScene, *frame.warpTable,
        motion.trainSceneIndex, motion.head);
    const GroupDoorBase doorBase =
        ResolveGroupDoorBase(group.trainId, trainGraph, motion.diaResult, poses);
    // _CABINVIEW対象車両の解決。無駄な
    // 走査を避けるため、運転台視点が追従中の編成の時だけ解決する。
    std::optional<rs::world::Guid> cabinLeadCarEntityId;
    if (group.trainId == frame.cabinViewTrainId) {
        rs::world::Guid leadCarEntityId = rs::world::NilGuid();
        bool tailCabinUnused = false;
        if (ResolveCabinLeadCarEntityId(*frame.registry, group.trainId,
                                        runtime_.GroupTravelReversed(group.trainId),
                                        leadCarEntityId, tailCabinUnused)) {
            cabinLeadCarEntityId = std::move(leadCarEntityId);
        }
    }
    for (size_t index = 0; index < group.slotIndices.size(); ++index) {
        Slot& slot = (*frame.slots)[group.slotIndices[index]];
        const rs::world::SceneEntity* entity = frame.registry->FindEntity(slot.entityId);
        if (UpdateCarSlotSnapshot(
                slot, entity, poses[index], index, group.slotIndices.size(), group.trainId,
                motion.diaResult, doorBase, *frame.activeGraph, frame.activeSceneIndex,
                *frame.constGraphForScene, *frame.warpTable, frame.dt, motion.dsMeters,
                motion.groupMoving, cabinLeadCarEntityId)) {
            ++acc.processedVehicles;
        }
    }
}

void SceneTrainController::Tick(
    rs::world::SceneRegistry& registry,
    const std::function<rs::world::TrackGraph*(size_t)>& graphForScene,
    const rs::world::WarpLinkTable& warpTable, size_t activeSceneIndex, double dt,
    double nowAbsSeconds, std::vector<SceneSlotPool::Slot>& slots,
    const SceneSlotPool& slotPool, StaticMeshCache& staticMeshCache,
    TrainId cabinViewTrainId, double timeScale) {
    lastTickStats_ = {};
    ++tickCounter_;
    if (lastActiveSceneIndex_.has_value() && *lastActiveSceneIndex_ != activeSceneIndex) {
        ResetWarpFade(slots);
    }
    lastActiveSceneIndex_ = activeSceneIndex;
    static const bool kProfileFrame = rs::platform::ReadEnvironmentVariable("RS_PROFILE_FRAME") != nullptr;
    const ProfileClock::time_point tickStart = ProfileClock::now();
    const rs::world::TrackGraph* activeGraph = graphForScene(activeSceneIndex);
    if (!activeGraph) return;
    const std::function<const rs::world::TrackGraph*(size_t)> constGraphForScene =
        [&graphForScene](size_t sceneIndex) -> const rs::world::TrackGraph* {
        return graphForScene(sceneIndex);
    };
    const std::vector<TrainGroupSnapshot> groups =
        CollectTrainGroups(registry, slots, slotPool, activeSceneIndex, &warnedEntityIds_);
    const std::vector<rs::sim::OccupancyTrain> occupancyTrains =
        BuildOccupancyTrains(groups, graphForScene);
    // フェーズ①(在線再構築)は
    // RunSignalBlockPhase(SceneSignalPhase.h/.cpp)が受け持つ。戻り値の
    // previousOccupiedBlockGuidsはフェーズ④(条件レイヤ)が使う。
    const SignalPhaseOutputs signalPhase = RunSignalBlockPhase(
        graphForScene, kUnboundedScanSceneCount, occupancyTrains,
        blockOccupancyTracker_);
    RecomputeSignalAspects(graphForScene, kUnboundedScanSceneCount, warpTable,
                           blockOccupancyTracker_, signalAspectRuntime_);

    TickFrame frame;
    frame.registry = &registry;
    frame.graphForScene = &graphForScene;
    frame.constGraphForScene = &constGraphForScene;
    frame.warpTable = &warpTable;
    frame.activeSceneIndex = activeSceneIndex;
    frame.activeGraph = activeGraph;
    frame.dt = dt;
    frame.nowAbsSeconds = nowAbsSeconds;
    frame.slots = &slots;
    frame.cabinViewTrainId = cabinViewTrainId;
    frame.timeScale = timeScale;
    frame.occupancyTrains = &occupancyTrains;
    frame.signalPhase = &signalPhase;
    TickAccumulators acc;
    for (const TrainGroupSnapshot& group : groups) {
        if (group.leadEntity == nullptr || group.leadPlacement == nullptr) continue;
        // 読込中で性能値が揃っていない編成は動かさない(最高速0扱いで現在速度が0に潰れるため)。
        if (group.hasUnresolvedCars) continue;
        GroupMotion motion;
        motion.head = group.leadPlacement->position;
        motion.trainSceneIndex = group.leadEntity->placementSceneIndex;
        const rs::world::TrackGraph* trainGraph = graphForScene(motion.trainSceneIndex);
        if (trainGraph) TickGroupOnGraph(group, *trainGraph, frame, acc, motion);
        UpdateGroupCarSlots(group, trainGraph, frame, motion, acc);
    }
    rs::world::TrackGraph* const activeGraphMutable = graphForScene(activeSceneIndex);
    if (activeGraphMutable != nullptr) {
        std::vector<std::string> log;
        stationRunInfoDispatcher_.ApplyDirtyEvents(registry, staticMeshCache,
                                                   *activeGraphMutable, log);
    }
    lastApproachMasks_ = rs::sim::ComputePlatformApproachAcrossScenes(
        constGraphForScene, warpTable, acc.approachTrains, acc.approachForwardSpans,
        rs::sim::SimTuning::Embedded().approach);
    lastTickStats_.searchMs = ToMilliseconds(acc.searchTime);
    lastTickStats_.kinematicsMs = ToMilliseconds(acc.kinematicsTime);
    if (kProfileFrame) {
        const double totalMs = ToMilliseconds(ProfileClock::now() - tickStart);
        spdlog::debug("[profile-train] total={:.2f}ms groups={} vehicles={} search={:.2f}ms "
                     "kinematics={:.2f}ms",
                     totalMs, groups.size(), acc.processedVehicles, lastTickStats_.searchMs,
                     lastTickStats_.kinematicsMs);
    }
}

} // namespace rs::app

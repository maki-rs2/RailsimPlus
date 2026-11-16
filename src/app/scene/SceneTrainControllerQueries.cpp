// 編成の問い合わせ・設定の窓口(重なり判定・ピッキング候補・速度/停止/タグ/運行情報の転送・編成の削除)。
#include "SceneTrainController.h"

#include <algorithm>
#include <cstring>
#include <utility>

#include "ScenePickItem.h"
#include "../../world/alignment/TrackForwardScan.h"

namespace rs::app {
namespace {
//描画と同じパーツ行列から作った箱を、ピッキング候補へ写す
void ApplyCarPickBox(const SceneSlotPool::CarRenderSnapshot& car, ScreenPickCandidate& candidate) {
    candidate.worldMatrix = car.carPickFrame;
    candidate.hasAabb = true;
    std::memcpy(candidate.aabbMin, car.carPickAabbMin, sizeof(candidate.aabbMin));
    std::memcpy(candidate.aabbMax, car.carPickAabbMax, sizeof(candidate.aabbMax));
}
} // namespace

bool SceneTrainController::WouldTrainOverlap(
    const rs::world::SceneRegistry& registry, const rs::world::TrackGraph& graph,
    const rs::world::TrackPosition& candidateHead, double candidateLengthM,
    const std::vector<SceneSlotPool::Slot>& slots, const SceneSlotPool& slotPool,
    size_t sceneIndex) const {
    rs::world::TrackPosition candidateBackwardStart = candidateHead;
    candidateBackwardStart.direction = -candidateBackwardStart.direction;
    const std::vector<rs::world::OccupiedRange> candidateRanges =
        rs::world::ScanOccupiedRangesAhead(graph, candidateBackwardStart,
                                           std::max(0.0, candidateLengthM));

    const std::vector<TrainGroupSnapshot> groups =
        CollectTrainGroups(registry, slots, slotPool, sceneIndex, &warnedEntityIds_);
    for (const TrainGroupSnapshot& group : groups) {
        if (group.leadEntity == nullptr || group.leadPlacement == nullptr ||
            group.leadEntity->placementSceneIndex != sceneIndex) continue;

        rs::world::TrackPosition backwardStart = group.leadPlacement->position;
        backwardStart.direction = -backwardStart.direction;
        const std::vector<rs::world::OccupiedRange> occupiedRanges =
            rs::world::ScanOccupiedRangesAhead(graph, backwardStart,
                                               std::max(0.0, group.trainLengthM));
        if (rs::world::OccupiedRangesOverlap(candidateRanges, occupiedRanges)) return true;
    }
    return false;
}

void SceneTrainController::CollectPickCandidates(const rs::world::SceneRegistry& registry,
                                                   const std::vector<SceneSlotPool::Slot>& slots,
                                                   std::vector<ScenePickItem>& out) const {
    out.clear();
    for (const Slot& slot : slots) {
        if (!slot.used || !slot.isTrainCar || !slot.carSnapshot.carVisible ||
            !slot.carSnapshot.carPickValid) continue;
        const rs::world::SceneEntity* entity = registry.FindEntity(slot.entityId);
        if (!entity) continue;
        ScenePickItem item;
        item.entityId = slot.entityId;
        ApplyCarPickBox(slot.carSnapshot, item.candidate);
        item.isStaticInstanced = false;
        out.push_back(std::move(item));
    }
}

bool SceneTrainController::TryGetPickCandidate(const rs::world::SceneRegistry& registry,
                                                  const rs::world::Guid& entityId,
                                                  const std::vector<SceneSlotPool::Slot>& slots,
                                                  int slotIndex, ScenePickItem& out) const {
    if (slotIndex < 0 || !slots[static_cast<size_t>(slotIndex)].isTrainCar ||
        !slots[static_cast<size_t>(slotIndex)].carSnapshot.carVisible ||
        !slots[static_cast<size_t>(slotIndex)].carSnapshot.carPickValid) return false;
    const rs::world::SceneEntity* entity = registry.FindEntity(entityId);
    if (!entity) return false;
    const Slot& slot = slots[static_cast<size_t>(slotIndex)];
    out.entityId = entityId;
    ApplyCarPickBox(slot.carSnapshot, out.candidate);
    out.isStaticInstanced = false;
    return true;
}

void SceneTrainController::SetGroupTargetSpeed(TrainId trainId, double targetSpeedKmh) { runtime_.SetGroupTargetSpeed(trainId, targetSpeedKmh); }
void SceneTrainController::SetGroupCurrentSpeed(TrainId trainId, double speedKmh) { runtime_.SetGroupCurrentSpeed(trainId, speedKmh); }
double SceneTrainController::GroupTargetSpeedKmh(TrainId trainId) const { return runtime_.GroupTargetSpeedKmh(trainId); }
double SceneTrainController::GroupCurrentSpeedKmh(TrainId trainId) const { return runtime_.GroupCurrentSpeedKmh(trainId); }
void SceneTrainController::SetGroupStopDistance(TrainId trainId, double remainingMeters) { runtime_.SetGroupStopDistance(trainId, remainingMeters); }
void SceneTrainController::ClearGroupStopTarget(TrainId trainId) { runtime_.ClearGroupStopTarget(trainId); }
bool SceneTrainController::GroupArrivedAtStop(TrainId trainId) const { return runtime_.GroupArrivedAtStop(trainId); }
rs::sim::DiaState SceneTrainController::GroupDiaState(TrainId trainId) const { return dia_.GroupState(trainId); }
bool SceneTrainController::GroupDepartureAbsSeconds(TrainId trainId, double& outAbsSeconds) const { return dia_.GroupDepartureAbsSeconds(trainId, outAbsSeconds); }
int SceneTrainController::PlatformRoutineCursorIndex(const rs::world::Guid& platformId, const std::vector<rs::world::PlatformRoutineElement>& routine, int layerKey, TrainId trainId) const { return dia_.PlatformRoutineCursorIndex(platformId, routine, layerKey, trainId); }
int SceneTrainController::AddTrainTag(const std::string& label) { return dia_.TagRegistry().AddTag(label); }
void SceneTrainController::RestoreTrainTag(rs::world::TrainTagDef tag) { dia_.TagRegistry().RestoreTag(std::move(tag)); }
void SceneTrainController::ResetForNewLayout() {
    warnedEntityIds_.clear();
    runtime_.Clear();
    dia_.Clear();
    dia_.TagRegistry().ClearTags();
    dia_.RunInfoRegistry() = rs::world::TrainRunInfoRegistry{};
    routeReservation_.Clear();
    deniedSignals_.clear();
    blockOccupancyTracker_.Clear();
    signalAspectRuntime_.Clear();
    cachedForwardConstraints_.clear();
    lastKnownTargetPlatform_.clear();
    lastStopLabels_.clear();
    lastApproachMasks_.clear();
}

void SceneTrainController::RemoveTrainTag(int tagId) { dia_.TagRegistry().RemoveTag(tagId); }
const std::vector<rs::world::TrainTagDef>& SceneTrainController::TrainTagDefs() const { return dia_.TagRegistry().Tags(); }
const std::vector<int>& SceneTrainController::TrainTagsOf(TrainId trainId) const { return dia_.TagRegistry().TrainTags(trainId); }
const std::unordered_map<TrainId, rs::world::TrainRunInfo, rs::world::GuidHash>& SceneTrainController::TrainRunInfoAssignments() const { return dia_.RunInfoRegistry().Assignments(); }
void SceneTrainController::SetTrainRunInfo(TrainId trainId, rs::world::TrainRunInfo info) { dia_.RunInfoRegistry().SetRunInfo(trainId, std::move(info)); }
void SceneTrainController::RemoveGroup(TrainId trainId, rs::world::SceneRegistry& registry) {
    std::vector<rs::world::Guid> idsToRemove;
    for (const rs::world::SceneEntity& entity : registry.Entities()) {
        if (entity.category == rs::world::SceneEntityCategory::Car &&
            entity.trainId == trainId) idsToRemove.push_back(entity.guid);
    }
    for (const rs::world::Guid& id : idsToRemove) registry.RemoveEntity(id);
    runtime_.RemoveGroup(trainId);
    dia_.RemoveGroup(trainId);
    lastKnownTargetPlatform_.erase(trainId);
    lastStopLabels_.erase(trainId);
    cachedForwardConstraints_.erase(trainId);
    routeReservation_.RemoveTrain(trainId);
    deniedSignals_.erase(trainId);
}

const SceneTrainController::StopLabels& SceneTrainController::GroupStopLabels(TrainId trainId) const {
    static const StopLabels kEmpty;
    const std::unordered_map<TrainId, StopLabels, rs::world::GuidHash>::const_iterator it =
        lastStopLabels_.find(trainId);
    return it != lastStopLabels_.end() ? it->second : kEmpty;
}


} // namespace rs::app

#include "../gpu/App.h"
#include "ParsedPluginCache.h"
#include "SceneRenderer.h"

#include "../camera/CameraControl.h"

#include <algorithm>
#include <chrono>
#include <optional>
#include <utility>

#include "../../plugins/load/ModelMeshHelpers.h"
#include "../../render/resource/RenderTuning.h"
#include "../../sim/OccupiedNodes.h"
#include "StaticMeshVariantBake.h"
#include "StationRailEffects.h"

namespace rs::app {

bool SceneRenderer::TakePluginCacheChange() {
    const uint64_t generation = GetParsedPluginCache().Generation();
    if (viewportPluginGeneration_ == generation) return false;
    viewportPluginGeneration_ = generation;
    return true;
}

void SceneRenderer::SetPlacementPreview(App& app,
                                        const std::optional<PlacementPreviewSpec>& preview,
                                        std::vector<std::string>& log) {
    trainPreviewShown_ = false;
    meshPreviewShown_ = false;
    if (!preview.has_value()) {
        app.SetPlacementPreviewActive(false);
        return;
    }

    const std::string key = MakePluginMeshKey("S|" + MakePluginSourceKey(preview->folderUtf8, ""));
    if (placementPreviewKey_ != key) {
        std::vector<std::string> loadLog;
        rs::app::GetParsedPluginCache().LoadInto(placementPreviewSession_, preview->folderUtf8, loadLog);
        log.insert(log.end(), loadLog.begin(), loadLog.end());
        placementPreviewKey_.clear();
        if (!placementPreviewSession_.plugin.ok || !placementPreviewSession_.plugin.hasAssembly) {
            app.SetPlacementPreviewActive(false);
            return;
        }
        // 透明度は描画側(FrameRenderer)が材質αに掛けて決めるので、ここでは材質を変えない。
        const rs::render::Mesh previewMesh = BakeSessionAssemblyMesh(placementPreviewSession_);
        if (!app.UploadPlacementPreviewMesh(previewMesh)) {
            app.SetPlacementPreviewActive(false);
            return;
        }
        placementPreviewKey_ = key;
    }
    app.SetPlacementPreviewWorlds({preview->world});
    app.SetPlacementPreviewActive(true);
}

void SceneRenderer::SetMeshPlacementPreview(App& app, const std::string& key,
                                            const std::function<rs::render::Mesh()>& build) {
    trainPreviewShown_ = false;
    const std::string fullKey = "M|" + key;
    if (placementPreviewKey_ != fullKey) {
        placementPreviewKey_.clear();
        const rs::render::Mesh mesh = build();
        if (mesh.vertices.empty() || !app.UploadPlacementPreviewMesh(mesh)) {
            app.SetPlacementPreviewActive(false);
            meshPreviewShown_ = false;
            return;
        }
        placementPreviewKey_ = fullKey;
    }
    app.SetPlacementPreviewWorlds({rs::core::Mat4::Identity()});
    app.SetPlacementPreviewActive(true);
    meshPreviewShown_ = true;
}

void SceneRenderer::ClearMeshPlacementPreview(App& app) {
    if (!meshPreviewShown_) return;
    app.SetPlacementPreviewActive(false);
    meshPreviewShown_ = false;
}

void SceneRenderer::SetTrainPlacementPreview(App& app, const std::optional<TrainPreviewSpec>& preview,
                                             std::vector<std::string>& log) {
    const bool hasCars = preview.has_value() && preview->cars != nullptr &&
                         preview->graph != nullptr && !preview->cars->empty();
    if (!hasCars) {
        if (trainPreviewShown_) {
            app.SetPlacementPreviewActive(false);
            trainPreviewShown_ = false;
        }
        return;
    }

    // 超える車は先頭から上限までを描く(gPartWorldの要素数がkSceneEntityMaxPartsまで)。
    const bool changed = trainPreviewModel_.Update(*preview->cars, kSceneEntityMaxParts, log);
    const std::string key = "T|" + trainPreviewModel_.ContentKey();
    if (changed || placementPreviewKey_ != key) {
        placementPreviewKey_.clear();
        trainPreviewShown_ = false;
        if (trainPreviewModel_.DrawnCarCount() == 0) {
            app.SetPlacementPreviewActive(false);
            return;
        }
        if (!app.UploadPlacementPreviewMesh(trainPreviewModel_.Mesh())) {
            app.SetPlacementPreviewActive(false);
            return;
        }
        placementPreviewKey_ = key;
    }
    if (trainPreviewModel_.DrawnCarCount() == 0) return;
    app.SetPlacementPreviewWorlds(
        trainPreviewModel_.SolveWorlds(*preview->cars, preview->head, *preview->graph));
    app.SetPlacementPreviewActive(true);
    trainPreviewShown_ = true;
}

std::string SceneRenderer::MakeStaticInstanceKey(const std::string& folderUtf8, const std::vector<int32_t>& switchOptions, bool signalModel, const std::string& definitionFile) { return MakePluginMeshKey(rs::app::MakeStaticInstanceKey(folderUtf8, switchOptions, signalModel, definitionFile)); }
bool SceneRenderer::TryGetStaticInstanceMesh(const std::string& folderUtf8, const std::vector<int32_t>& switchOptions, std::vector<std::string>& log, const rs::render::Mesh** outMesh, bool signalModel, const std::string& definitionFile) { return staticMeshCache_.TryGetStaticInstanceMesh(folderUtf8, switchOptions, log, outMesh, signalModel, definitionFile); }
bool SceneRenderer::TryGetStaticInstanceVariants(const std::string& folderUtf8, const std::vector<int32_t>& switchOptions, std::vector<std::string>& log, const StaticMeshVariantSet** outVariants, bool signalModel, const std::string& definitionFile) { return staticMeshCache_.TryGetStaticInstanceVariants(folderUtf8, switchOptions, log, outVariants, signalModel, definitionFile); }
bool SceneRenderer::TryGetTrackPluginAssemblyMesh(const std::string& folderUtf8, std::vector<std::string>& log, const rs::render::Mesh** outMesh) { return staticMeshCache_.TryGetTrackPluginAssemblyMesh(folderUtf8, log, outMesh); }
bool SceneRenderer::TryGetPierAssemblyMesh(const std::string& folderUtf8, float heightM, std::vector<std::string>& log, const rs::render::Mesh** outMesh) { return staticMeshCache_.TryGetPierAssemblyMesh(folderUtf8, heightM, log, outMesh); }
const std::vector<rs::legacy_pi::SwitchDef>* SceneRenderer::TryGetSwitchDefs(const std::string& folderUtf8, std::vector<std::string>& log) { return staticMeshCache_.TryGetSwitchDefs(folderUtf8, log); }
const rs::legacy_pi::sidecar::StationSidecar& SceneRenderer::TryGetStationSidecar(const std::string& folderUtf8) { return staticMeshCache_.TryGetStationSidecar(folderUtf8); }
void SceneRenderer::ApplyBandedStaticInstances(App& app) {
    const double nowSeconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    const float updateHz = rs::render::RenderTuning::Embedded().camDistBandUpdateHz;
    const double intervalSeconds = updateHz > 0.0f ? 1.0 / static_cast<double>(updateHz) : 0.0;
    rs::render::InstanceBatchManager& batches = app.InstanceBatches();
    bandedStaticInstances_.ApplyPending(
        nowSeconds, intervalSeconds, lastSharedSwitches_,
        [&batches](const std::unordered_set<std::string>& keys) { batches.ClearInstances(keys); },
        [&batches](const std::string& key, const float world[16]) { batches.AddInstance(key, world); });
}
void SceneRenderer::UpdateBandedStaticInstances(App& app, const float eye[3], float viewerFovYRadians, const rs::world::SceneSharedSwitchValues& shared) {
    bandedStaticInstances_.SetView(eye, viewerFovYRadians);
    lastSharedSwitches_ = shared;
    ApplyBandedStaticInstances(app);
}
void SceneRenderer::PruneStaticMeshCache(const std::unordered_set<std::string>& liveKeys) { staticMeshCache_.PruneStaticMeshCache(liveKeys); }
void SceneRenderer::PruneTrackPluginAssemblyCache(const std::unordered_set<std::string>& liveKeys) { staticMeshCache_.PruneTrackPluginAssemblyCache(liveKeys); }
void SceneRenderer::PrunePierAssemblyCache(const std::unordered_set<std::string>& liveKeys) { staticMeshCache_.PrunePierAssemblyCache(liveKeys); }
void SceneRenderer::Sync(App& app, const rs::world::SceneRegistry& registry, std::vector<std::string>& log) { synchronizer_.Sync(app, registry, staticMeshCache_, externalMeshes_, log); }
void SceneRenderer::Tick(App& app, rs::world::SceneRegistry& registry, const std::function<rs::world::TrackGraph*(size_t)>& graphForScene, const rs::world::WarpLinkTable& warpTable, size_t activeSceneIndex, double dt, double nowAbsSeconds, uint64_t activeGraphRevision, const rs::world::GameClock& clock, const rs::world::SolarState& sun, double latitudeDeg, double nightThresholdSin, TrainId cabinViewTrainId, const float eye[3], float viewerFovYRadians) {
    std::vector<Slot>& slots = Slots();
    const std::chrono::steady_clock::time_point trainStart = std::chrono::steady_clock::now();
    trainController_.Tick(registry, graphForScene, warpTable, activeSceneIndex, dt,
                            nowAbsSeconds, slots, synchronizer_.SlotPool(), staticMeshCache_,
                            cabinViewTrainId, clock.TimeScale());
    // 駅のレール接続エフェクタ。使用中ノードは、効果が実際に働く瞬間にだけ列車の占有経路から求める。
    if (rs::world::TrackGraph* activeGraph = graphForScene(activeSceneIndex)) {
        const auto constGraphForScene = [&graphForScene](size_t sceneIndex) -> const rs::world::TrackGraph* {
            return graphForScene(sceneIndex);
        };
        std::optional<rs::world::NodeInUseFn> inUseCache;
        const rs::world::NodeInUseFn inUse = [&](const rs::world::Guid& node) {
            if (!inUseCache)
                inUseCache = rs::sim::MakeNodeInUseFn(rs::sim::CollectOccupiedNodes(
                    trainController_.Runtime().AllOccupiedPaths(), constGraphForScene));
            return (*inUseCache)(node);
        };
        ApplyStationRailEffects(*activeGraph, registry, slots, inUse);
    }
    const std::chrono::steady_clock::time_point sceneryStart = std::chrono::steady_clock::now();
    sceneryRuntime_.Tick(app, registry, graphForScene(activeSceneIndex), slots, activeGraphRevision, dt, trainController_.LastApproachMasks(), clock, sun, latitudeDeg, nightThresholdSin, eye, viewerFovYRadians);
    const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
    lastTrainTickMs_ = std::chrono::duration<double, std::milli>(sceneryStart - trainStart).count();
    lastSceneryTickMs_ = std::chrono::duration<double, std::milli>(end - sceneryStart).count();
}
SceneSlotPool::Slot* SceneRenderer::FindSlotForEntity(const rs::world::Guid& entityId) {
    const int slotIndex = FindSlotByEntityId(entityId);
    return slotIndex < 0 ? nullptr : &Slots()[static_cast<size_t>(slotIndex)];
}
PluginSession* SceneRenderer::FindSessionByEntityId(const rs::world::Guid& entityId) {
    Slot* slot = FindSlotForEntity(entityId);
    return slot != nullptr && slot->session.plugin.ok ? &slot->session : nullptr;
}
void SceneRenderer::UpdateSceneAnimations(App& app) { sceneryRuntime_.UpdateAnimations(app, Slots()); }
void SceneRenderer::SubmitTrainRenderSnapshot(App& app, const rs::world::TrackGraph& activeGraph,
                                                const float eye[3], const rs::world::GameClock& clock,
                                                const rs::world::SolarState& sun, double latitudeDeg,
                                                double nightThresholdSin, float viewerFovYRadians,
                                                const WheelSoundFrame& wheelFrame,
                                                std::vector<rs::sim::WheelSoundEvent>& wheelEvents) {
    trainController_.SubmitRenderSnapshot(app, activeGraph, eye, clock, sun, latitudeDeg,
                                            nightThresholdSin, Slots(), viewerFovYRadians,
                                            wheelFrame, wheelEvents);
}
void SceneRenderer::ResetTrainWarpFade() { trainController_.ResetWarpFade(Slots()); }
void SceneRenderer::RemapTrainSceneIndices(const std::vector<size_t>& oldToNew) { trainController_.RemapSceneIndices(oldToNew); }
void SceneRenderer::CollectSceneryPickCandidates(const rs::world::SceneRegistry& registry, const rs::world::TrackGraph& graph, std::vector<std::string>& log, std::vector<ScenePickItem>& out) { sceneryPicker_.Collect(registry, graph, staticMeshCache_, Slots(), [this](const rs::world::Guid& id) { return FindSlotByEntityId(id); }, log, out);
    SceneSceneryPicker::AppendSignalItems(signalModels_, graph, staticMeshCache_, log, out); }
bool SceneRenderer::TryGetSceneryPickCandidate(const rs::world::SceneRegistry& registry, const rs::world::TrackGraph& graph, const rs::world::Guid& entityId, std::vector<std::string>& log, ScenePickItem& out) { return sceneryPicker_.TryGet(registry, graph, staticMeshCache_, Slots(), [this](const rs::world::Guid& id) { return FindSlotByEntityId(id); }, entityId, log, out) || SceneSceneryPicker::TryGetSignalItem(signalModels_, graph, staticMeshCache_, entityId, log, out); }
void SceneRenderer::CollectTrainPickCandidates(const rs::world::SceneRegistry& registry, std::vector<ScenePickItem>& out) const { trainController_.CollectPickCandidates(registry, Slots(), out); }
bool SceneRenderer::TryGetTrainPickCandidate(const rs::world::SceneRegistry& registry, const rs::world::Guid& entityId, ScenePickItem& out) const { return trainController_.TryGetPickCandidate(registry, entityId, Slots(), FindSlotByEntityId(entityId), out); }
void SceneRenderer::SetGroupTargetSpeed(TrainId id, double value) { trainController_.SetGroupTargetSpeed(id, value); }
void SceneRenderer::SetGroupCurrentSpeed(TrainId id, double value) { trainController_.SetGroupCurrentSpeed(id, value); }
double SceneRenderer::GroupTargetSpeedKmh(TrainId id) const { return trainController_.GroupTargetSpeedKmh(id); }
double SceneRenderer::GroupCurrentSpeedKmh(TrainId id) const { return trainController_.GroupCurrentSpeedKmh(id); }
rs::sim::TrainRuntime& SceneRenderer::TrainRuntime() { return trainController_.Runtime(); }
const rs::sim::TrainRuntime& SceneRenderer::TrainRuntime() const { return trainController_.Runtime(); }
rs::sim::DiaRuntime& SceneRenderer::DiaRuntime() { return trainController_.DiaRuntime(); }
const rs::sim::DiaRuntime& SceneRenderer::DiaRuntime() const { return trainController_.DiaRuntime(); }
void SceneRenderer::SetGroupStopDistance(TrainId id, double value) { trainController_.SetGroupStopDistance(id, value); }
void SceneRenderer::ClearGroupStopTarget(TrainId id) { trainController_.ClearGroupStopTarget(id); }
bool SceneRenderer::GroupArrivedAtStop(TrainId id) const { return trainController_.GroupArrivedAtStop(id); }
rs::sim::DiaState SceneRenderer::GroupDiaState(TrainId id) const { return trainController_.GroupDiaState(id); }
bool SceneRenderer::GroupDepartureAbsSeconds(TrainId id, double& outAbsSeconds) const { return trainController_.GroupDepartureAbsSeconds(id, outAbsSeconds); }
bool SceneRenderer::ResolveCabinPivot(const rs::world::SceneRegistry& registry, TrainId trainId,
                                      bool travelReversed, bool& outTailCabin,
                                      rs::core::Mat4& outPivotWorld) const {
    return trainController_.ResolveCabinPivot(registry, synchronizer_.Slots(), trainId,
                                              travelReversed, outTailCabin, outPivotWorld);
}
bool SceneRenderer::ResolveLeadCarAnchor(const rs::world::SceneRegistry& registry, TrainId trainId,
                                         bool travelReversed, rs::core::Vec3& outAnchor) const {
    return rs::app::ResolveLeadCarAnchor(registry, synchronizer_.Slots(), trainId, travelReversed, outAnchor);
}
void SceneRenderer::UpdateCameraFollow(const rs::world::SceneRegistry& registry, CameraController& camera) {
    if (camera.IsFollowing()) {
        const TrainId trainId = camera.FollowedTrainId();
        rs::core::Vec3 anchor;
        if (ResolveLeadCarAnchor(registry, trainId, TrainRuntime().GroupTravelReversed(trainId), anchor)) {
            camera.UpdateFollow(anchor);
        } else {
            camera.StopFollow();
        }
    }
    const std::optional<rs::world::SurfaceBounds>& bounds = CurrentSurfaceBounds(registry);
    if (bounds) camera.ClampTargetToSurface(*bounds);
}
const SceneTrainController::StopLabels& SceneRenderer::GroupStopLabels(TrainId id) const {
    return trainController_.GroupStopLabels(id);
}
int SceneRenderer::PlatformRoutineCursorIndex(const rs::world::Guid& platformId, const std::vector<rs::world::PlatformRoutineElement>& routine, int layerKey, TrainId trainId) const { return trainController_.PlatformRoutineCursorIndex(platformId, routine, layerKey, trainId); }
int SceneRenderer::AddTrainTag(const std::string& label) { return trainController_.AddTrainTag(label); }
void SceneRenderer::RestoreTrainTag(rs::world::TrainTagDef tag) { trainController_.RestoreTrainTag(std::move(tag)); }
void SceneRenderer::ResetForNewLayout() { trainController_.ResetForNewLayout(); }
void SceneRenderer::RemoveTrainTag(int tagId) { trainController_.RemoveTrainTag(tagId); }
const std::vector<rs::world::TrainTagDef>& SceneRenderer::TrainTagDefs() const { return trainController_.TrainTagDefs(); }
const std::vector<int>& SceneRenderer::TrainTagsOf(TrainId trainId) const { return trainController_.TrainTagsOf(trainId); }
const std::unordered_map<TrainId, rs::world::TrainRunInfo, rs::world::GuidHash>& SceneRenderer::TrainRunInfoAssignments() const { return trainController_.TrainRunInfoAssignments(); }
void SceneRenderer::SetTrainRunInfo(TrainId trainId, rs::world::TrainRunInfo info) { trainController_.SetTrainRunInfo(trainId, std::move(info)); }
void SceneRenderer::RemoveGroup(TrainId id, rs::world::SceneRegistry& registry) { trainController_.RemoveGroup(id, registry); }
bool SceneRenderer::WouldTrainOverlap(const rs::world::SceneRegistry& registry,
                                        const rs::world::TrackGraph& graph,
                                        const rs::world::TrackPosition& candidateHead,
                                        double candidateLengthM, size_t sceneIndex) const {
    return trainController_.WouldTrainOverlap(registry, graph, candidateHead,
                                                  candidateLengthM, Slots(), synchronizer_.SlotPool(),
                                                  sceneIndex);
}

} // namespace rs::app

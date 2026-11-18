#include "SceneSceneryPicker.h"
#include "StaticMeshCache.h"
#include "SurfaceBoundsTracker.h"
#include "../../world/scene/ScenePlacement.h"
#include <cstring>
#include <functional>
#include "../../world/alignment/TrackGraph.h"
namespace rs::app {
namespace {
ScenePickItem BuildItem(const rs::world::SceneEntity& entity, const rs::world::TrackGraph& graph, StaticMeshCache& staticMeshCache, std::vector<SceneSlotPool::Slot>& slots, const std::function<int(const rs::world::Guid&)>& findSlotByEntityId, std::vector<std::string>& log) {
    ScenePickItem item;
    item.entityId = entity.guid;
    item.folderUtf8 = entity.folderUtf8;
    item.switchOptions = entity.switchOptions;
    item.definitionFile = entity.definitionFile;
    item.signalModel = entity.isSignalModel;
    item.candidate.worldMatrix = rs::world::ResolveWorldMatrix(entity.placement, graph);

    const rs::render::Mesh* instancedMesh = nullptr;
    if (rs::world::MayUseStaticInstancing(entity) && staticMeshCache.TryGetStaticInstanceMesh(entity.folderUtf8, entity.switchOptions, log, &instancedMesh,
                                                 entity.isSignalModel, entity.definitionFile) &&
        instancedMesh && !instancedMesh->vertices.empty()) {
        item.isStaticInstanced = true;
        item.candidate.hasAabb = true;
        std::memcpy(item.candidate.aabbMin, instancedMesh->aabbMin,
                   sizeof(item.candidate.aabbMin));
        std::memcpy(item.candidate.aabbMax, instancedMesh->aabbMax,
                   sizeof(item.candidate.aabbMax));
    } else {
        // 静的インスタンス化対象から外れたScenery(可動部/時間経過アニメ持ち、
        // 発車標プラグイン等)はslotPool_側で個別セッションを持つ。
        const int slotIdx = findSlotByEntityId(entity.guid);
        if (slotIdx >= 0) {
            SceneSlotPool::Slot& slot = slots[static_cast<size_t>(slotIdx)];
            if (slot.sceneryPick.sceneryPickHasAabb) {
                item.candidate.hasAabb = true;
                std::memcpy(item.candidate.aabbMin, slot.sceneryPick.sceneryPickAabbMin,
                           sizeof(item.candidate.aabbMin));
                std::memcpy(item.candidate.aabbMax, slot.sceneryPick.sceneryPickAabbMax,
                           sizeof(item.candidate.aabbMax));
            }
        }
    }
    return item;
}

// EnsureCache()はregistry.Revision()変化時のみ再構築する(SceneEntity一覧の
// 増減検出用)一方、SceneRegistry::UpdatePlacement()はRevision()を進めない
// (SceneSceneryRuntime.cpp Tick()冒頭のコメント参照)。そのためcandidate.worldMatrix
// をBuildItem時点のまま使い回すと、配置移動後もキャッシュ済みの古い変換行列と
// 新しいAABB(sceneryPick*、Tickが毎フレーム更新)を組み合わせてしまい、
// 「AABBは合っているのに判定位置がズレる」状態になる。AABBだけでなくworldMatrixも
// Collect/TryGetのたびに最新のentity.placementから再解決する。
void RefreshRuntimeState(std::vector<ScenePickItem>& items, const rs::world::SceneRegistry& registry,
                         const rs::world::TrackGraph& graph, std::vector<SceneSlotPool::Slot>& slots,
                         const std::function<int(const rs::world::Guid&)>& findSlotByEntityId) {
    for (ScenePickItem& item : items) {
        const rs::world::SceneEntity* entity = registry.FindEntity(item.entityId);
        if (entity) item.candidate.worldMatrix = rs::world::ResolveWorldMatrix(entity->placement, graph);

        if (item.isStaticInstanced) continue;
        item.candidate.hasAabb = false;
        const int slotIdx = findSlotByEntityId(item.entityId);
        if (slotIdx < 0) continue;
        SceneSlotPool::Slot& slot = slots[static_cast<size_t>(slotIdx)];
        if (!slot.sceneryPick.sceneryPickHasAabb) continue;
        item.candidate.hasAabb = true;
        std::memcpy(item.candidate.aabbMin, slot.sceneryPick.sceneryPickAabbMin,
                    sizeof(item.candidate.aabbMin));
        std::memcpy(item.candidate.aabbMax, slot.sceneryPick.sceneryPickAabbMax,
                    sizeof(item.candidate.aabbMax));
    }
}

// 信号モデルは個別セッション(slots)を持たないので、静的インスタンス化できず箱が取れない信号は
// hasAabb=falseのまま(位置の点でだけ拾われ、赤い枠は出ない)。
ScenePickItem BuildSignalItem(const rs::world::SceneEntity& entity, const rs::world::TrackGraph& graph,
                              StaticMeshCache& staticMeshCache, std::vector<std::string>& log) {
    std::vector<SceneSlotPool::Slot> noSlots;
    return BuildItem(entity, graph, staticMeshCache, noSlots, [](const rs::world::Guid&) { return -1; }, log);
}

} // namespace

void SceneSceneryPicker::AppendSignalItems(const SignalModelSet& signalModels, const rs::world::TrackGraph& graph,
                                           StaticMeshCache& staticMeshCache, std::vector<std::string>& log,
                                           std::vector<ScenePickItem>& out) {
    for (const rs::world::SceneEntity& entity : signalModels.Entities()) {
        out.push_back(BuildSignalItem(entity, graph, staticMeshCache, log));
    }
}

bool SceneSceneryPicker::TryGetSignalItem(const SignalModelSet& signalModels, const rs::world::TrackGraph& graph,
                                          StaticMeshCache& staticMeshCache, const rs::world::Guid& entityId,
                                          std::vector<std::string>& log, ScenePickItem& out) {
    const rs::world::SceneEntity* entity = signalModels.FindEntity(entityId);
    if (entity == nullptr) return false;
    out = BuildSignalItem(*entity, graph, staticMeshCache, log);
    return true;
}

void SceneSceneryPicker::EnsureCache(const rs::world::SceneRegistry& registry, const rs::world::TrackGraph& graph, StaticMeshCache& staticMeshCache, std::vector<SceneSlotPool::Slot>& slots, const std::function<int(const rs::world::Guid&)>& findSlotByEntityId, std::vector<std::string>& log) {
    const uint64_t generation = staticMeshCache.Generation();
    if (registry.Revision() == lastRevision_ && generation == lastCacheGeneration_) return;
    lastCacheGeneration_ = generation;
    lastRevision_ = registry.Revision();
    cachedItems_.clear();
    for (const rs::world::SceneEntity& entity : registry.Entities()) {
        if (entity.category != rs::world::SceneEntityCategory::Scenery) continue;
        if (!rs::world::IsUserPlacement(entity)) continue;
        // 地形(Surface)は配置・選択の対象にしない(下の信号機や踏切のホバーを奪わない)。
        if (IsSurfacePluginFolder(entity.folderUtf8)) continue;
        cachedItems_.push_back(BuildItem(entity, graph, staticMeshCache, slots, findSlotByEntityId, log));
    }
}

void SceneSceneryPicker::Collect(const rs::world::SceneRegistry& registry, const rs::world::TrackGraph& graph, StaticMeshCache& staticMeshCache, std::vector<SceneSlotPool::Slot>& slots, const std::function<int(const rs::world::Guid&)>& findSlotByEntityId, std::vector<std::string>& log, std::vector<ScenePickItem>& out) {
    EnsureCache(registry, graph, staticMeshCache, slots, findSlotByEntityId, log);
    RefreshRuntimeState(cachedItems_, registry, graph, slots, findSlotByEntityId);
    out = cachedItems_;
}

bool SceneSceneryPicker::TryGet(const rs::world::SceneRegistry& registry, const rs::world::TrackGraph& graph, StaticMeshCache& staticMeshCache, std::vector<SceneSlotPool::Slot>& slots, const std::function<int(const rs::world::Guid&)>& findSlotByEntityId, const rs::world::Guid& entityId, std::vector<std::string>& log, ScenePickItem& out) {
    EnsureCache(registry, graph, staticMeshCache, slots, findSlotByEntityId, log);
    RefreshRuntimeState(cachedItems_, registry, graph, slots, findSlotByEntityId);
    for (const ScenePickItem& item : cachedItems_) {
        if (item.entityId == entityId) {
            out = item;
            return true;
        }
    }
    return false;
}
} // namespace rs::app

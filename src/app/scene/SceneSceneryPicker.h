#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <functional>

#include "ScenePickItem.h"
#include "SceneSlotPool.h"
#include "SignalModelSet.h"
#include "../../world/scene/SceneRegistry.h"
#include "../../world/alignment/TrackGraph.h"

namespace rs::app {

class StaticMeshCache;

class SceneSceneryPicker {
public:
    void Collect(const rs::world::SceneRegistry& registry, const rs::world::TrackGraph& graph,
                 StaticMeshCache& staticMeshCache, std::vector<SceneSlotPool::Slot>& slots,
                 const std::function<int(const rs::world::Guid&)>& findSlotByEntityId,
                 std::vector<std::string>& log, std::vector<ScenePickItem>& out);
    bool TryGet(const rs::world::SceneRegistry& registry, const rs::world::TrackGraph& graph,
                StaticMeshCache& staticMeshCache, std::vector<SceneSlotPool::Slot>& slots,
                const std::function<int(const rs::world::Guid&)>& findSlotByEntityId,
                const rs::world::Guid& entityId, std::vector<std::string>& log, ScenePickItem& out);

    // 信号モデル(SceneRegistryに入らない描画用エンティティ)の候補。モデルは信号の増減や現示で
    // 変わるので、キャッシュに混ぜず呼ぶたびに作る。outの末尾へ足す(空にはしない)。
    static void AppendSignalItems(const SignalModelSet& signalModels, const rs::world::TrackGraph& graph,
                                  StaticMeshCache& staticMeshCache, std::vector<std::string>& log,
                                  std::vector<ScenePickItem>& out);
    static bool TryGetSignalItem(const SignalModelSet& signalModels, const rs::world::TrackGraph& graph,
                                 StaticMeshCache& staticMeshCache, const rs::world::Guid& entityId,
                                 std::vector<std::string>& log, ScenePickItem& out);

private:
    void EnsureCache(const rs::world::SceneRegistry& registry, const rs::world::TrackGraph& graph,
                     StaticMeshCache& staticMeshCache, std::vector<SceneSlotPool::Slot>& slots,
                     const std::function<int(const rs::world::Guid&)>& findSlotByEntityId,
                     std::vector<std::string>& log);
    std::vector<ScenePickItem> cachedItems_;
    uint64_t lastCacheGeneration_ = UINT64_MAX;
    uint64_t lastRevision_ = UINT64_MAX;
};

} // namespace rs::app

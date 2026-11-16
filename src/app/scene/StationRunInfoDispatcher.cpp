#include "StationRunInfoDispatcher.h"

#include <spdlog/spdlog.h>

#include "../../plugins/schema/sidecar/StationSidecar.h"
#include "../../plugins/schema/sidecar/SwitchAssignmentResolve.h"
#include "../../world/alignment/TrackGraph.h"
#include "../../world/scene/SceneRegistry.h"
#include "StaticMeshCache.h"

namespace rs::app {

void StationRunInfoDispatcher::MarkPlatformLocked(const rs::world::Guid& platformId,
                                                   const rs::world::TrainRunInfo& info) {
    pending_.push_back({platformId, info});
}

void StationRunInfoDispatcher::MarkPlatformArrived(const rs::world::Guid& platformId,
                                                    const rs::world::TrainRunInfo& info) {
    pending_.push_back({platformId, info});
}

void StationRunInfoDispatcher::MarkPlatformDeparted(const rs::world::Guid& platformId) {
    pending_.push_back({platformId, std::nullopt});
}

void StationRunInfoDispatcher::ApplyDirtyEvents(
    rs::world::SceneRegistry& registry, StaticMeshCache& cache,
    const rs::world::TrackGraph& graph, std::vector<std::string>& log) {
    for (const StationRunInfoDirtyEntry& entry : pending_) {
        const rs::world::PlatformStopInfo* platform = nullptr;
        for (const rs::world::PlatformStopInfo& candidate : graph.platforms) {
            if (candidate.id == entry.platformId) {
                platform = &candidate;
                break;
            }
        }
        if (platform == nullptr || rs::world::GuidIsNil(platform->stationEntity)) continue;

        for (const rs::world::SceneEntity& entity : registry.Entities()) {
            if (entity.guid != platform->stationEntity) continue;
            const rs::legacy_pi::sidecar::StationSidecar& sidecar =
                cache.TryGetStationSidecar(entity.folderUtf8);
            if (!sidecar.loaded) continue;

            if (!entry.runInfo.has_value()) {
                registry.ClearRuntimeSwitchOptions(entity.guid);
                continue;
            }

            const rs::legacy_pi::sidecar::SwitchAssignment assignment =
                rs::legacy_pi::sidecar::BuildRunInfoSwitchAssignment(
                    sidecar.table, entry.runInfo->type, entry.runInfo->destination,
                    entry.runInfo->lineName, entry.runInfo->trainNumber, entry.runInfo->dutyNumber);
            if (assignment.empty()) continue;
            const std::vector<rs::legacy_pi::SwitchDef>* switchDefs =
                cache.TryGetSwitchDefs(entity.folderUtf8, log);
            if (switchDefs != nullptr)
                ApplySwitchAssignment(registry, entity.guid, *switchDefs, assignment, entity.folderUtf8);
        }
    }
    pending_.clear();
}

bool StationRunInfoDispatcher::ApplySwitchAssignment(
    rs::world::SceneRegistry& registry, const rs::world::Guid& entityId,
    const std::vector<rs::legacy_pi::SwitchDef>& switchDefs,
    const rs::legacy_pi::sidecar::SwitchAssignment& assignment,
    const std::string& folderUtf8) {
    const rs::world::SceneEntity* entity = registry.FindEntity(entityId);
    if (entity == nullptr) return false;

    std::vector<std::string> ignoredKeys;
    const std::vector<std::pair<size_t, int32_t>> changes =
        rs::legacy_pi::sidecar::ResolveSwitchAssignmentChanges(
            switchDefs, entity->switchOptions, assignment, &ignoredKeys);

    bool applied = false;
    for (const std::pair<size_t, int32_t>& change : changes) {
        // 運行に合わせた表示なので保存値は変えない。
        registry.SetRuntimeSwitchOption(entityId, change.first, change.second);
        applied = true;
    }

    if (!ignoredKeys.empty()) {
        std::string ignoredKeysText;
        for (size_t index = 0; index < ignoredKeys.size(); ++index) {
            if (index != 0) ignoredKeysText += ", ";
            ignoredKeysText += ignoredKeys[index];
        }
        spdlog::warn("[plugin-sidecar] folder '{}' ignored unknown or system switch labels: {}",
                     folderUtf8, ignoredKeysText);
    }
    return applied;
}

} // namespace rs::app

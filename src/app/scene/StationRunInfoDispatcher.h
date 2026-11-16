#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "../../world/alignment/Guid.h"
#include "../../plugins/schema/sidecar/SwitchAssignmentResolve.h"
#include "../../world/scene/TrainRunInfoRegistry.h"

namespace rs::world {
struct TrackGraph;
class SceneRegistry;
}

namespace rs::app {
class StaticMeshCache;

struct StationRunInfoDirtyEntry {
    rs::world::Guid platformId;
    std::optional<rs::world::TrainRunInfo> runInfo;
};

class StationRunInfoDispatcher {
public:
    // 運行表示は保存値と分け、定義の取得元にレジストリの書き換えを持ち込まない。
    bool ApplySwitchAssignment(
        rs::world::SceneRegistry& registry, const rs::world::Guid& entityId,
        const std::vector<rs::legacy_pi::SwitchDef>& switchDefs,
        const rs::legacy_pi::sidecar::SwitchAssignment& assignment,
        const std::string& folderUtf8);

    void MarkPlatformLocked(const rs::world::Guid& platformId,
                            const rs::world::TrainRunInfo& info);
    void MarkPlatformArrived(const rs::world::Guid& platformId,
                             const rs::world::TrainRunInfo& info);
    void MarkPlatformDeparted(const rs::world::Guid& platformId);

    void ApplyDirtyEvents(rs::world::SceneRegistry& registry, StaticMeshCache& cache,
                          const rs::world::TrackGraph& graph, std::vector<std::string>& log);

private:
    std::vector<StationRunInfoDirtyEntry> pending_;
};

} // namespace rs::app

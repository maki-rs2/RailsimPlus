#include "StationRailEffects.h"

#include <optional>

#include "../../plugins/schema/PluginSchema.h"
#include "../../world/alignment/RailLinkOverlay.h"

namespace rs::app {
namespace {

using rs::legacy_pi::RailEffectorDef;

std::optional<rs::world::EdgeEnd> ResolveEnd(const rs::world::TrackGraph& graph, const std::string& address,
                                             int platformId, int platformEnd) {
    if (platformId < 0) return std::nullopt;
    return rs::world::ResolvePlatformEnd(graph, address, platformId, platformEnd);
}

bool ApplyOne(rs::world::TrackGraph& graph, const std::string& address, const RailEffectorDef& effector,
              const rs::world::NodeInUseFn& inUse) {
    const std::optional<rs::world::EdgeEnd> a = ResolveEnd(graph, address, effector.platformId1, effector.platformEnd1);
    if (!a) return false;
    if (effector.kind == RailEffectorDef::Kind::Disconnect)
        return rs::world::DisconnectEdgeEnd(graph, *a, inUse);
    const std::optional<rs::world::EdgeEnd> b = ResolveEnd(graph, address, effector.platformId2, effector.platformEnd2);
    if (!b) return false;
    return effector.kind == RailEffectorDef::Kind::Connect ? rs::world::ConnectEdgeEnds(graph, *a, *b, inUse)
                                                           : rs::world::BranchEdgeEnds(graph, *a, *b, inUse);
}

} // namespace

bool ApplyStationRailEffects(rs::world::TrackGraph& graph, const rs::world::SceneRegistry& registry,
                             const std::vector<SceneSlotPool::Slot>& slots,
                             const rs::world::NodeInUseFn& inUse) {
    bool changed = rs::world::PruneRailLinkOverlay(graph);
    for (const SceneSlotPool::Slot& slot : slots) {
        if (!slot.used || slot.isTrainCar || !slot.session.plugin.ok) continue;
        const std::vector<RailEffectorDef>& effectors = slot.session.plugin.def.railEffectors;
        if (effectors.empty()) continue;
        const rs::world::SceneEntity* entity = registry.FindEntity(slot.entityId);
        if (entity == nullptr || entity->sourceAddress.empty()) continue;
        const rs::legacy_pi::SwitchResolver resolver = [&slot](const std::string& name) -> int32_t {
            return slot.session.ResolveSwitch(name);
        };
        for (const RailEffectorDef& effector : effectors) {
            if (!rs::legacy_pi::IsTriggerActive(effector.trig, resolver)) continue;
            changed = ApplyOne(graph, entity->sourceAddress, effector, inUse) || changed;
        }
    }
    return changed;
}

} // namespace rs::app

#include "PlatformParentDriver.h"

#include <cstring>

#include "../../world/alignment/RigidPose.h"

namespace rs::app {

void UpdatePlatformParentDeltas(rs::world::TrackGraph& graph, const rs::world::Guid& entityGuid,
                                const rs::legacy_pi::PluginDefinition& def,
                                const std::vector<rs::core::Mat4>& dynamicWorlds,
                                const rs::core::Mat4& entityWorld) {
    for (const rs::world::PlatformStopInfo& platform : graph.platforms) {
        if (platform.stationEntity != entityGuid) continue;
        for (const rs::world::Guid& edgeId : platform.railEdges) {
            const auto edgeIt = graph.edges.find(edgeId);
            if (edgeIt == graph.edges.end()) continue;
            rs::world::TrackEdge& edge = edgeIt->second;
            if (!rs::world::IsParentDrivenEdge(edge) || edge.parentEntity != entityGuid) continue;
            const size_t part = static_cast<size_t>(edge.parentPart);
            if (part >= def.parts.size() || part >= dynamicWorlds.size()) continue;
            rs::core::Mat4 restPart;
            std::memcpy(restPart.m, def.parts[part].worldMat, sizeof(restPart.m));
            edge.parentDelta = rs::world::RigidDelta(
                rs::world::NormalizedRigidPose(rs::core::Mat4::Mul(restPart, entityWorld)),
                rs::world::NormalizedRigidPose(rs::core::Mat4::Mul(dynamicWorlds[part], entityWorld)));
        }
    }
}

void ResetOrphanedParentDeltas(rs::world::TrackGraph& graph,
                               const std::function<bool(const rs::world::Guid&)>& entityExists) {
    // edge全件ではなく、駅の配置を持つ番線(少数)から引く。
    for (const rs::world::PlatformStopInfo& platform : graph.platforms) {
        if (rs::world::GuidIsNil(platform.stationEntity) || entityExists(platform.stationEntity)) continue;
        for (const rs::world::Guid& edgeId : platform.railEdges) {
            const auto edgeIt = graph.edges.find(edgeId);
            if (edgeIt == graph.edges.end() || !rs::world::IsParentDrivenEdge(edgeIt->second)) continue;
            edgeIt->second.parentDelta = rs::world::RigidPose{};
        }
    }
}

void ResetAllParentDeltas(rs::world::TrackGraph& graph) {
    for (auto& [guid, edge] : graph.edges) {
        (void)guid;
        if (rs::world::IsParentDrivenEdge(edge)) edge.parentDelta = rs::world::RigidPose{};
    }
}

} // namespace rs::app

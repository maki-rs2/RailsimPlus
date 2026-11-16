#include "Editor.h"
#include "SceneEntityCommand.h"

#include <memory>

#include "../../plugins/load/PluginDefinitionLookup.h"
#include "../../plugins/schema/PluginSchema.h"
#include "../../world/alignment/TrackTraversal.h"
#include "../../world/scene/SceneEntity.h"
#include "../../world/scene/ScenePlacement.h"
#include "../../world/scene/SceneRegistry.h"

namespace rs::editor {

std::optional<rs::world::FreePlacement> Editor::StationAnchorPlacement(const rs::world::Guid& edge,
                                                                       double s,
                                                                       int direction) const {
    const std::unordered_map<rs::world::Guid, rs::world::TrackEdge, rs::world::GuidHash>::const_iterator
        edgeIt = graph.edges.find(edge);
    if (edgeIt == graph.edges.end() || edgeIt->second.spline.size() < 2) return std::nullopt;
    const rs::world::TrackFrame frame = rs::world::EvaluateAt(edgeIt->second, s, direction < 0 ? -1 : +1);
    return rs::world::DecomposeFreePlacement(frame.pos, frame.tangent, frame.up);
}

bool Editor::AddStationAnchor(rs::world::SceneRegistry& sceneRegistry,
                              const rs::world::Guid& edge, double s, int direction,
                              const std::string& folderUtf8) {
    const std::optional<rs::world::FreePlacement> placement = StationAnchorPlacement(edge, s, direction);
    if (!placement.has_value() || folderUtf8.empty()) return false;

    rs::world::SceneEntity entity;
    entity.guid = rs::world::GenerateGuid();
    entity.folderUtf8 = folderUtf8;
    entity.category = rs::world::SceneEntityCategory::Scenery;
    entity.placement = *placement;
    SceneEntityCommand::Change change;
    change.after = entity;
    change.afterIndex = sceneRegistry.Entities().size();
    std::unique_ptr<CompositeCommand> command = std::make_unique<CompositeCommand>();
    command->Add(std::make_unique<SceneEntityCommand>(std::vector{std::move(change)}));
    const rs::legacy_pi::ParsedDefinitionResult parsed = rs::legacy_plugin::LoadPluginDefinition(folderUtf8);
    if (parsed.ok && !parsed.def.platforms.empty()) {
        const rs::legacy_pi::PlatformDef& definition = parsed.def.platforms.front();
        rs::world::PlatformStopInfo platform;
        platform.id = rs::world::GenerateGuid();
        platform.stationEntity = entity.guid;
        platform.indexInStation = 0;
        platform.railEdges = {edge};
        platform.stoppable = definition.stoppable;
        platform.openDoor[0] = definition.openDoor[0];
        platform.openDoor[1] = definition.openDoor[1];
        command->Add(std::make_unique<AddPlatformCommand>(platform));
    }
    if (!ExecuteCommand(std::move(command), &sceneRegistry)) return false;
    NotePlaced();

    lastPlacedTravel = PlacedTravel{edge, direction < 0 ? -1 : +1};
    return true;
}

std::unique_ptr<ICommand> Editor::RemoveStationAnchorPlatform(const rs::world::Guid& sceneEntityId) {
    std::unique_ptr<CompositeCommand> command = std::make_unique<CompositeCommand>();
    for (const rs::world::PlatformStopInfo& platform : graph.platforms) {
        if (platform.stationEntity == sceneEntityId)
            command->Add(std::make_unique<RemovePlatformCommand>(platform.id));
    }
    if (command->Empty()) return nullptr;
    return command;
}

} // namespace rs::editor

#include "Editor.h"

#include <algorithm>
#include <iterator>
#include <memory>
#include <utility>

#include "../../world/scene/SceneRegistry.h"

namespace rs::editor {

void Editor::DeleteSelectedSceneEntities(rs::world::SceneRegistry& sceneRegistry) {
    if (selectedSceneEntityIds_.Empty()) return;
    std::unique_ptr<CompositeCommand> command = std::make_unique<CompositeCommand>();
    std::vector<SceneEntityCommand::Change> changes;
    const std::vector<rs::world::SceneEntity>& entities = sceneRegistry.Entities();
    for (const rs::world::Guid& id : selectedSceneEntityIds_.Items()) {
        for (size_t i = 0; i < entities.size(); ++i) {
            if (entities[i].guid != id || entities[i].category != rs::world::SceneEntityCategory::Scenery)
                continue;
            SceneEntityCommand::Change change;
            change.guid = id;
            change.before = entities[i];
            change.beforeIndex = i;
            changes.push_back(std::move(change));
            if (std::unique_ptr<ICommand> removal = RemoveStationAnchorPlatform(id))
                command->Add(std::move(removal));
            break;
        }
    }
    if (changes.empty()) return;
    command->Add(std::make_unique<SceneEntityCommand>(std::move(changes)));
    if (!ExecuteCommand(std::move(command), &sceneRegistry)) return;
    NoteRemoved();
    ClearSelectedSceneEntity();
}

void Editor::CommitSceneEntityPlacements(
    rs::world::SceneRegistry& sceneRegistry,
    const std::vector<std::pair<rs::world::Guid, rs::world::FreePlacement>>& updates) {
    std::vector<SceneEntityCommand::Change> changes;
    for (const auto& [id, placement] : updates) {
        const rs::world::SceneEntity* entity = sceneRegistry.FindEntity(id);
        if (entity == nullptr || entity->category != rs::world::SceneEntityCategory::Scenery) continue;
        const rs::world::FreePlacement* current = std::get_if<rs::world::FreePlacement>(&entity->placement);
        if (current == nullptr) continue;
        if (current->position.x == placement.position.x && current->position.y == placement.position.y &&
            current->position.z == placement.position.z && current->yawDeg == placement.yawDeg &&
            current->pitchDeg == placement.pitchDeg && current->rollDeg == placement.rollDeg) continue;
        SceneEntityCommand::Change change;
        change.guid = id;
        change.before = *entity;
        change.after = *entity;
        change.after->placement = placement;
        const std::vector<rs::world::SceneEntity>& entities = sceneRegistry.Entities();
        const auto found = std::find_if(entities.begin(), entities.end(), [&id](const auto& item) {
            return item.guid == id;
        });
        change.beforeIndex = change.afterIndex = static_cast<size_t>(std::distance(entities.begin(), found));
        changes.push_back(std::move(change));
    }
    if (changes.empty()) return;
    ExecuteCommand(std::make_unique<SceneEntityCommand>(std::move(changes)), &sceneRegistry);
}

void Editor::CommitSceneEntityTransform(
    rs::world::SceneRegistry& sceneRegistry,
    const std::vector<rs::world::SceneEntity>& before) {
    std::vector<SceneEntityCommand::Change> changes;
    for (const rs::world::SceneEntity& oldEntity : before) {
        const rs::world::SceneEntity* current = sceneRegistry.FindEntity(oldEntity.guid);
        if (current == nullptr || current->category != rs::world::SceneEntityCategory::Scenery) continue;
        const rs::world::FreePlacement* oldPlacement = std::get_if<rs::world::FreePlacement>(&oldEntity.placement);
        const rs::world::FreePlacement* newPlacement = std::get_if<rs::world::FreePlacement>(&current->placement);
        if (oldPlacement == nullptr || newPlacement == nullptr) continue;
        if (oldPlacement->position.x == newPlacement->position.x &&
            oldPlacement->position.y == newPlacement->position.y &&
            oldPlacement->position.z == newPlacement->position.z &&
            oldPlacement->yawDeg == newPlacement->yawDeg &&
            oldPlacement->pitchDeg == newPlacement->pitchDeg &&
            oldPlacement->rollDeg == newPlacement->rollDeg) continue;
        SceneEntityCommand::Change change;
        change.guid = oldEntity.guid;
        change.before = oldEntity;
        change.after = *current;
        const std::vector<rs::world::SceneEntity>& entities = sceneRegistry.Entities();
        const auto found = std::find_if(entities.begin(), entities.end(), [&oldEntity](const auto& item) {
            return item.guid == oldEntity.guid;
        });
        change.beforeIndex = change.afterIndex = static_cast<size_t>(std::distance(entities.begin(), found));
        changes.push_back(std::move(change));
    }
    if (changes.empty()) return;
    ExecuteCommand(std::make_unique<SceneEntityCommand>(std::move(changes)), &sceneRegistry);
}

} // namespace rs::editor

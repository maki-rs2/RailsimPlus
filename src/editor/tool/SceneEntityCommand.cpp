#include "SceneEntityCommand.h"

#include <algorithm>

#include "../../world/scene/SceneRegistry.h"

namespace rs::editor {

void SceneEntityCommand::Apply(rs::world::SceneRegistry& registry) { ApplyState(registry, true); }

void SceneEntityCommand::Revert(rs::world::SceneRegistry& registry) { ApplyState(registry, false); }

void SceneEntityCommand::ApplyState(rs::world::SceneRegistry& registry, bool after) {
    std::vector<Change*> ordered;
    for (Change& change : changes_) ordered.push_back(&change);
    // 複数挿入は低い添字から戻さないと、先に戻した要素が後の挿入で押し出される。
    std::stable_sort(ordered.begin(), ordered.end(), [after](const Change* a, const Change* b) {
        return (after ? a->afterIndex : a->beforeIndex) <
               (after ? b->afterIndex : b->beforeIndex);
    });
    // 削除を先に済ませ、挿入先の添字を最終状態に対して解釈する。
    for (Change* change : ordered) {
        if (!(after ? change->after : change->before)) registry.RemoveEntity(change->guid);
    }
    for (Change* item : ordered) {
        Change& change = *item;
        std::optional<rs::world::SceneEntity>& target = after ? change.after : change.before;
        if (!target) continue;
        const std::optional<rs::world::SceneEntity>& other = after ? change.before : change.after;
        const rs::world::Guid guid = !rs::world::GuidIsNil(change.guid)
            ? change.guid
            : (other.has_value() ? other->guid : rs::world::NilGuid());
        if (!rs::world::GuidIsNil(guid)) {
            if (registry.FindEntity(guid) == nullptr) {
                target->guid = guid;
                const size_t index = after ? change.afterIndex : change.beforeIndex;
                const rs::world::Guid restored = registry.RestoreEntityAt(index, *target);
                change.guid = restored;
                target->guid = restored;
            } else {
                registry.CommitPlacement(guid, target->placement);
            }
        } else if (target.has_value()) {
            const size_t index = after ? change.afterIndex : change.beforeIndex;
            change.guid = registry.RestoreEntityAt(index, *target);
            target->guid = change.guid;
        }
    }
}

} // namespace rs::editor

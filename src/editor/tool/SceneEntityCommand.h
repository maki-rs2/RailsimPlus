#pragma once

#include <optional>
#include <cstddef>
#include <utility>
#include <vector>

#include "Command.h"
#include "../../world/scene/SceneEntity.h"

namespace rs::editor {

// SceneRegistryの編集をTrackGraphの編集と同じCommandStack順序へ積む。
class SceneEntityCommand final : public ICommand {
public:
    struct Change {
        rs::world::Guid guid = rs::world::NilGuid();
        std::optional<rs::world::SceneEntity> before;
        std::optional<rs::world::SceneEntity> after;
        size_t beforeIndex = 0;
        size_t afterIndex = 0;
    };

    explicit SceneEntityCommand(std::vector<Change> changes) : changes_(std::move(changes)) {
        for (Change& change : changes_) {
            if (rs::world::GuidIsNil(change.guid)) {
                if (change.before) change.guid = change.before->guid;
                else if (change.after) change.guid = change.after->guid;
            }
        }
    }
    void Apply(rs::world::TrackGraph&) override {}
    void Revert(rs::world::TrackGraph&) override {}
    void Apply(rs::world::SceneRegistry& registry) override;
    void Revert(rs::world::SceneRegistry& registry) override;

private:
    void ApplyState(rs::world::SceneRegistry& registry, bool after);
    std::vector<Change> changes_;
};

} // namespace rs::editor

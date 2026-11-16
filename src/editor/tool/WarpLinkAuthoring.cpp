#include "WarpLinkAuthoring.h"

#include "Command.h"
#include "Editor.h"
#include "../../world/scene/SceneIndexRemap.h"

namespace rs::editor {

void WarpLinkAuthoring::BeginPending(size_t sceneIndex, const rs::world::Guid& node) {
    pending_ = rs::world::WarpEndpoint{sceneIndex, node};
}

void WarpLinkAuthoring::CancelPending() { pending_.reset(); }

void WarpLinkAuthoring::RemapScenes(const std::vector<size_t>& oldToNew) {
    if (!pending_.has_value()) return;
    pending_->sceneIndex = rs::world::RemapSceneIndex(oldToNew, pending_->sceneIndex);
    // 消えるSceneの端点が保留中なら、保留を解除する。
    if (rs::world::IsRemovedScene(pending_->sceneIndex)) pending_.reset();
}

bool WarpLinkAuthoring::HasPending() const { return pending_.has_value(); }

std::optional<rs::world::WarpEndpoint> WarpLinkAuthoring::Pending() const { return pending_; }

bool WarpLinkAuthoring::ConfirmPending(size_t sceneIndex, const rs::world::Guid& node,
                                        Editor& editor,
                                        rs::world::WarpLinkTable& warpTable) {
    if (!pending_.has_value()) return false;

    const rs::world::WarpEndpoint pending = *pending_;
    if (pending.sceneIndex == sceneIndex && pending.node == node) {
        // 自己リンクになるため無意味。保留は解除しない(選び直しを許す)。
        return false;
    }

    const rs::world::WarpEndpoint other{sceneIndex, node};
    if (!editor.ExecuteCommand(std::make_unique<AddWarpLinkCommand>(warpTable, pending, other)))
        return false;
    pending_.reset();
    return true;
}

} // namespace rs::editor

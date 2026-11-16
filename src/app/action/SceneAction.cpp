#include "SceneAction.h"

#include <format>
#include <string>

#include "LayoutPersistenceAction.h"
#include "ToastQueue.h"
#include "../locale/Localization.h"

namespace rs::app::scene_action {
namespace {
// 読込中は、シーンの操作を受け付けずに理由を伝える。
bool Accept(const LayoutPersistenceAction& action, ToastQueue& queue) {
    if (!action.IsLoading()) return true;
    queue.Post(ToastSeverity::Info, "読込中はシーンを操作できません");
    return false;
}
} // namespace

void SwitchScene(LayoutPersistenceAction& action, ToastQueue& queue, int row) {
    if (row >= 0 && Accept(action, queue)) action.SwitchSceneTo(static_cast<size_t>(row));
}
void PreviousScene(LayoutPersistenceAction& action, ToastQueue& queue) {
    if (Accept(action, queue)) action.SwitchScene(-1);
}
void NextScene(LayoutPersistenceAction& action, ToastQueue& queue) {
    if (Accept(action, queue)) action.SwitchScene(+1);
}
void RenameScene(LayoutPersistenceAction& action, ToastQueue& queue, int row, std::string_view name) {
    if (row >= 0 && Accept(action, queue)) action.RenameScene(static_cast<size_t>(row), std::string(name));
}
void ReorderScene(LayoutPersistenceAction& action, ToastQueue& queue, int source, int destination) {
    if (source >= 0 && destination >= 0 && Accept(action, queue)) {
        action.ReorderScene(static_cast<size_t>(source), static_cast<size_t>(destination));
    }
}
bool AddScene(LayoutPersistenceAction& action, ToastQueue& queue, const std::string& surfaceFolderUtf8) {
    if (surfaceFolderUtf8.empty() || !Accept(action, queue)) return false;
    if (action.AddScene(surfaceFolderUtf8).Ok()) return true;
    queue.Post(ToastSeverity::Warning, "シーンを追加できませんでした");
    return false;
}
bool DeleteScene(LayoutPersistenceAction& action, ToastQueue& queue, const Localization& localization, int row) {
    if (row < 0 || !Accept(action, queue)) return false;
    if (action.SceneViewModel().sceneCount <= 1) {
        queue.Post(ToastSeverity::Info, localization.Text("scene.delete.last_scene"));
        return false;
    }
    if (!action.DeleteScene(static_cast<size_t>(row))) {
        queue.Post(ToastSeverity::Warning, localization.Text("scene.delete.failed"));
        return false;
    }
    const std::string name = action.DeletedSceneName();
    // ボタンは、トーストを出した時点の控えではなく「いまの控え」を戻す(控えが古ければ失敗を伝える)。
    queue.PostWithAction(ToastSeverity::Info, std::vformat(localization.Text("scene.delete.done"), std::make_format_args(name)),
                         localization.Text("scene.delete.undo"), [&action, &queue, &localization]() {
                             RestoreDeletedScene(action, queue, localization);
                         });
    return true;
}
bool RestoreDeletedScene(LayoutPersistenceAction& action, ToastQueue& queue, const Localization& localization) {
    if (!Accept(action, queue)) return false;
    const std::string name = action.DeletedSceneName();
    if (!action.RestoreDeletedScene()) {
        queue.Post(ToastSeverity::Warning, localization.Text("scene.restore.failed"));
        return false;
    }
    queue.Post(ToastSeverity::Info, std::vformat(localization.Text("scene.restore.done"), std::make_format_args(name)));
    return true;
}
void ApplyEnvironment(LayoutPersistenceAction& action, ToastQueue& queue, int row, const std::string& envPlugin) {
    if (row >= 0 && Accept(action, queue)) action.SetSceneEnvironment(static_cast<size_t>(row), envPlugin);
}
}

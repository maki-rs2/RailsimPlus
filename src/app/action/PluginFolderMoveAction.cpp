#include "PluginFolderMoveAction.h"

#include "NotImplementedNotice.h"

namespace rs::app::plugin_folder_move_action {
void MoveToFolder(ToastQueue& queue, std::string_view, std::string_view) {
    NotifyNotImplemented(queue, "プラグインのフォルダの移動");
}
} // namespace rs::app::plugin_folder_move_action

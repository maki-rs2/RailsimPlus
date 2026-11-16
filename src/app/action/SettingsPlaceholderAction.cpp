#include "SettingsPlaceholderAction.h"

#include "NotImplementedNotice.h"

namespace rs::app {
namespace game_settings_action {
void SetMouseOperation(ToastQueue& queue, const std::string&) { NotifyNotImplemented(queue, "マウス操作の変更"); }
} // namespace game_settings_action
} // namespace rs::app

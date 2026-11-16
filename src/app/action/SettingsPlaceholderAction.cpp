#include "SettingsPlaceholderAction.h"

#include "NotImplementedNotice.h"

namespace rs::app {
namespace game_settings_action {
void SetEffectsVolume(ToastQueue& queue, int) { NotifyNotImplemented(queue, "効果音の音量変更"); }
void SetMouseOperation(ToastQueue& queue, const std::string&) { NotifyNotImplemented(queue, "マウス操作の変更"); }
} // namespace game_settings_action
} // namespace rs::app

#pragma once

#include <string>

namespace rs::app {
class ToastQueue;
namespace game_settings_action {
void SetEffectsVolume(ToastQueue& queue, int percent);
void SetMouseOperation(ToastQueue& queue, const std::string& mode);
} // namespace game_settings_action
} // namespace rs::app

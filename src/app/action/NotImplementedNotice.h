#pragma once

#include <string_view>

namespace rs::app {
class ToastQueue;
void NotifyNotImplemented(ToastQueue& queue, std::string_view operation);
} // namespace rs::app

#include "NotImplementedNotice.h"

#include <string>

#include "ToastQueue.h"

namespace rs::app {
void NotifyNotImplemented(ToastQueue& queue, std::string_view operation) {
    queue.Post(ToastSeverity::Info, std::string(operation) + "は未実装です");
}
} // namespace rs::app

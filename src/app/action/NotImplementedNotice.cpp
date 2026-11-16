#include "NotImplementedNotice.h"

namespace rs::app {
// 処理が結びついていない操作を押しても、通知は出さず何もしない。
void NotifyNotImplemented(ToastQueue&, std::string_view) {}
} // namespace rs::app

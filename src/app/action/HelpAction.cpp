#include "HelpAction.h"

#include "NotImplementedNotice.h"

namespace rs::app::help_action {
void OpenTutorial(ToastQueue& queue) { NotifyNotImplemented(queue, "チュートリアル"); }
void OpenHelp(ToastQueue& queue) { NotifyNotImplemented(queue, "ヘルプを開く操作"); }
} // namespace rs::app::help_action

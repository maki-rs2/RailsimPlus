#include "TimeControlAction.h"

#include "GameSettingsAction.h"
#include "TimeControlDateTime.h"
#include "ToastQueue.h"

namespace rs::app::time_control_action {
void Pause(GameSettingsAction& action) { action.SetTimeScale(0.0f); }

void SetScale(GameSettingsAction& action, double scale) {
    action.SetTimeScale(static_cast<float>(scale));
}

void ChangeDateTime(GameSettingsAction& action, ToastQueue& queue, std::string_view date, std::string_view time) {
    const DateTimeParseResult parsed = ParseDateTimeInput(date, time);
    if (!parsed.value.has_value()) {
        queue.Post(ToastSeverity::Warning, parsed.error);
        return;
    }
    action.SetDateTime(parsed.value->date, parsed.value->hour, parsed.value->minute);
}
}

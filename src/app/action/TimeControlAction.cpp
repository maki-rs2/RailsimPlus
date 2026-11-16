#include "TimeControlAction.h"

#include "GameSettingsAction.h"
#include "TimeControlDateTime.h"
#include "ToastQueue.h"
#include "../../world/time/SolarModeNames.h"

#include <optional>

namespace rs::app::time_control_action {
void Pause(GameSettingsAction& action) { action.SetTimeScale(0.0f); }

void SetScale(GameSettingsAction& action, double scale) {
    action.SetTimeScale(static_cast<float>(scale));
}

bool ChangeDateTime(GameSettingsAction& action, ToastQueue& queue, std::string_view date, std::string_view time) {
    const DateTimeParseResult parsed = ParseDateTimeInput(date, time);
    if (!parsed.value.has_value()) {
        queue.Post(ToastSeverity::Warning, parsed.error);
        return false;
    }
    action.SetDateTime(parsed.value->date, parsed.value->hour, parsed.value->minute);
    return true;
}

void SetSolarModes(GameSettingsAction& action, std::string_view rotationKey, std::string_view revolutionKey) {
    const GameSettingsViewModel current = action.ViewModel();
    if (const std::optional<rs::world::EarthRotationMode> mode = rs::world::ParseEarthRotationMode(rotationKey);
        mode && static_cast<int>(*mode) != current.earthRotationMode) {
        action.SetEarthRotationMode(static_cast<int>(*mode));
    }
    if (const std::optional<rs::world::EarthRevolutionMode> mode = rs::world::ParseEarthRevolutionMode(revolutionKey);
        mode && static_cast<int>(*mode) != current.earthRevolutionMode) {
        action.SetEarthRevolutionMode(static_cast<int>(*mode));
    }
}
}

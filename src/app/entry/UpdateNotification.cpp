#include "UpdateNotification.h"

#include <format>
#include "AppRuntime.h"

namespace rs::app {

void StartUpdateCheck(AppRuntimeState& state) {
    if (!IsUnattendedRun(state.cli)) state.updateChecker.Request(state.userSettings);
}

void PollUpdateCheck(AppRuntimeState& state) {
    if (!state.updateChecker.Poll() || state.updateChecker.State() != UpdateCheckState::Available) return;
    const std::string& version = state.updateChecker.Release().version;
    if (state.userSettings.dismissedUpdateVersion == version) return;
    rmlui::RmlUiWorkspaceView* workspace = state.rmlUiIntegration.WorkspaceView();
    if (workspace == nullptr) return;
    workspace->Toasts().Post(ToastSeverity::Info,
        std::vformat(state.localization.Text("game_settings.about.update_toast"), std::make_format_args(version)));
    state.userSettings.dismissedUpdateVersion = version;
    SaveUserSettingsOrWarn(state.userSettings, "Failed to save notified update version.");
}
} // namespace rs::app

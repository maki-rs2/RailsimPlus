#include "AppRuntimeSettingsInit.h"

#include <windows.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <spdlog/spdlog.h>

#include "AppRuntime.h"
#include "EmbeddedLocaleStrings.h"
#include "../input/InputBindingOverrides.h"

namespace rs::app {
namespace {

// スモーク系の検証用の起動か。
bool IsDiagnosticRun(const AppCommandLine& cli) {
    return cli.smokeFrames > 0 || cli.switchFlipSmokeFrames > 0 || cli.sceneCycleSmokeSwitches > 0;
}

} // namespace

void ApplySavedWindowSettings(AppRuntimeState& state) {
    if (state.cli.rmluiWorkspace && !state.viewerMode && !IsDiagnosticRun(state.cli) &&
        state.cli.capturePngPath.empty()) {
        state.mainWindow.SetFullscreen(state.userSettings.fullscreen);
    }
}

void InitializeSettingsAndAutoRecovery(AppRuntimeState& state) {
    std::vector<std::string> localizationDiagnostics;
    state.localization = Localization::FromJson(
        std::string_view(reinterpret_cast<const char*>(rs::embedded::kLocaleStringsJson),
                         rs::embedded::kLocaleStringsJsonSize),
        localizationDiagnostics);
    for (const std::string& diagnostic : localizationDiagnostics) {
        spdlog::warn("{}", diagnostic);
    }

    const std::filesystem::path settingsPath = UserSettings::DefaultPath();
    std::error_code settingsExistsError;
    const bool settingsExists = std::filesystem::exists(settingsPath, settingsExistsError);
    state.userSettings = UserSettings::Load(settingsPath);
    if (!settingsExists && !settingsExistsError &&
        PRIMARYLANGID(::GetUserDefaultUILanguage()) == LANG_JAPANESE) {
        state.userSettings.language = Language::Japanese;
    }
    state.localization.SetLanguage(state.userSettings.language);
    state.input.SetBindings(input::BuildInputBindings(state.userSettings.keyBindings));
    state.autoRecoverySchedule.enabled = state.userSettings.autoRecoverEnabled;
    state.autoRecoverySchedule.intervalSeconds = state.userSettings.autoRecoverIntervalMinutes * 60;
    // 起動直後を最後の試行時刻とみなす(既定の0のままだと最初の編集で即座に書き出してしまう)。
    state.autoRecoverySchedule.lastAttemptSeconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    state.autoRecoveryRoot = DefaultAutoRecoveryRoot();
    state.previousRunCrashed = HasCrashMarker(state.autoRecoveryRoot);
    state.autoRecoveryCandidates = FindAutoRecoveryCandidates(state.autoRecoveryRoot);
    spdlog::info("[autorecover] previousCrash={} candidates={}", state.previousRunCrashed,
                 state.autoRecoveryCandidates.size());
    // 復元の画面はワークスペースの通常の起動でだけ出す。それ以外の起動がマーカーを消すと、次の通常の起動で
    // 画面が出なくなるので、マーカーには触らない。
    state.tracksCrashMarker = state.cli.rmluiWorkspace && !state.viewerMode && !IsDiagnosticRun(state.cli);
    if (!state.tracksCrashMarker) return;
    state.recoveryChoicePending = state.previousRunCrashed && !state.autoRecoveryCandidates.empty();
    if (!WriteCrashMarker(state.autoRecoveryRoot)) spdlog::warn("[autorecover] could not write crash marker");
}

} // namespace rs::app

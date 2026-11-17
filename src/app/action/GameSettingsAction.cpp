#include "GameSettingsAction.h"

#include "PluginRootAction.h"
#include "../plugin_session/LayoutDocumentState.h"
#include "../camera/CameraControl.h"
#include "../entry/MainWindow.h"
#include "../gpu/App.h"
#include "../update/UpdateChecker.h"
#include "../../sim/audio/AudioEngine.h"
#include "../../world/time/SolarModeNames.h"

namespace rs::app {
namespace {

} // namespace

GameSettingsAction::GameSettingsAction(const GameSettingsRefs& refs) : refs_(refs) {}

void GameSettingsAction::SetLanguage(Language language) {
    refs_.localization.SetLanguage(language);
    refs_.userSettings.language = language;
    SaveUserSettingsOrWarn(refs_.userSettings, "Failed to save user settings after language selection.");
}

void GameSettingsAction::SetUiTheme(const std::string& themeName) {
    refs_.userSettings.uiTheme = themeName;
    SaveUserSettingsOrWarn(refs_.userSettings, "Failed to save user settings after UI theme selection.");
}

void GameSettingsAction::SetUpdateCheckEnabled(bool enabled) {
    if (refs_.userSettings.updateCheckEnabled == enabled) return;
    refs_.userSettings.updateCheckEnabled = enabled;
    SaveUserSettingsOrWarn(refs_.userSettings, "Failed to save update check preference.");
}

void GameSettingsAction::CheckForUpdates() {
    if (refs_.updateChecker != nullptr) refs_.updateChecker->Request(refs_.userSettings, true);
}

void GameSettingsAction::OpenUpdateDownloadPage() {
    if (refs_.updateChecker != nullptr) refs_.updateChecker->OpenDownloadPage();
}

void GameSettingsAction::ResetCamera() {
    // カメラを初期状態(原点注視)へ戻す。
    refs_.camera.ResetActive();
}

void GameSettingsAction::SetTimeScale(float scale) {
    refs_.world.gameClock.SetTimeScale(rs::world::GameClock::ClampTimeScale(scale > 0.0f ? scale : 0.0f));
}

void GameSettingsAction::SetDateTime(const rs::world::DateParts& date, int hour, int minute) {
    constexpr double kSecondsPerMinute = 60.0;
    constexpr double kSecondsPerHour = 3600.0;
    refs_.world.gameClock.SetDateTime(rs::world::FromDateParts(date), hour * kSecondsPerHour + minute * kSecondsPerMinute);
}

void GameSettingsAction::SetFullscreen(bool enabled) {
    if (refs_.userSettings.fullscreen == enabled) return;
    refs_.userSettings.fullscreen = enabled;
    if (refs_.mainWindow != nullptr) refs_.mainWindow->SetFullscreen(enabled);
    SaveUserSettingsOrWarn(refs_.userSettings, "Failed to save user settings after fullscreen selection.");
}

void GameSettingsAction::SetViewOperation(ViewOperationMode mode) {
    if (refs_.userSettings.viewOperation == mode) return;
    refs_.userSettings.viewOperation = mode;
    SaveUserSettingsOrWarn(refs_.userSettings, "Failed to save user settings after view operation selection.");
}

void GameSettingsAction::SetEarthRotationMode(int mode) {
    refs_.world.solarOverrides.earthRotationMode = rs::world::EarthRotationModeFromIndex(mode);
    // 地球の自転の扱いは .rsl の world.environment に保存する値なので、レイアウトの編集として数える。
    refs_.document.MarkEdited();
}

void GameSettingsAction::SetEarthRevolutionMode(int mode) {
    refs_.world.solarOverrides.earthRevolutionMode = rs::world::EarthRevolutionModeFromIndex(mode);
    // 自転と同じく .rsl に保存する値なので、レイアウトの編集として数える。
    refs_.document.MarkEdited();
}

void GameSettingsAction::SetLayoutSaveDirectory(const std::string& directoryUtf8) {
    refs_.userSettings.layoutSaveDirectory = directoryUtf8;
    SaveUserSettingsOrWarn(refs_.userSettings, "Failed to save user settings after layout save directory selection.");
}

bool GameSettingsAction::AddPluginRoot(const std::string& rootUtf8) {
    return PluginRootAction(refs_.userSettings).Add(rootUtf8);
}

void GameSettingsAction::SetAudioOutputDevice(const std::string& deviceNameUtf8) {
    refs_.userSettings.audioOutputDeviceName = deviceNameUtf8;
    refs_.audioEngine.SetOutputDeviceName(deviceNameUtf8);
    SaveUserSettingsOrWarn(refs_.userSettings, "Failed to save user settings after audio output device selection.");
}

std::vector<std::string> GameSettingsAction::EnumerateAudioOutputDevices() const {
    return refs_.audioEngine.EnumerateOutputDevices();
}

void GameSettingsAction::SetAudioMasterVolume(float volume) {
    refs_.userSettings.audioMasterVolume = UserSettingsData::ClampAudioVolume(volume);
    refs_.audioEngine.SetUserMasterVolume(refs_.userSettings.audioMasterVolume);
}

void GameSettingsAction::CommitAudioMasterVolume() {
    CommitAudioCategoryVolumes();
}

void GameSettingsAction::SetAudioTrainVolume(float volume) {
    refs_.userSettings.audioTrainVolume = UserSettingsData::ClampAudioVolume(volume);
    refs_.audioEngine.SetUserTrainVolume(refs_.userSettings.audioTrainVolume);
}

void GameSettingsAction::SetAudioEnvironmentVolume(float volume) {
    refs_.userSettings.audioEnvironmentVolume = UserSettingsData::ClampAudioVolume(volume);
    refs_.audioEngine.SetUserEnvironmentVolume(refs_.userSettings.audioEnvironmentVolume);
}

void GameSettingsAction::SetAudioEffectsVolume(float volume) {
    refs_.userSettings.audioEffectsVolume = UserSettingsData::ClampAudioVolume(volume);
    refs_.audioEngine.SetUserEffectsVolume(refs_.userSettings.audioEffectsVolume);
}

void GameSettingsAction::CommitAudioCategoryVolumes() {
    SaveUserSettingsOrWarn(refs_.userSettings, "Failed to save user settings after audio volume change.");
}

GameSettingsViewModel GameSettingsAction::ViewModel() const {
    GameSettingsViewModel viewModel;
    const rs::world::GameClock& clock = refs_.world.gameClock;
    viewModel.language = refs_.localization.CurrentLanguage();
    viewModel.uiTheme = refs_.userSettings.uiTheme;
    viewModel.year = clock.Year();
    viewModel.month = clock.Month();
    viewModel.day = clock.Day();
    const int dayOfWeek = clock.DayOfWeek();
    viewModel.dayOfWeek = dayOfWeek >= 0 && dayOfWeek < 7 ? dayOfWeek : 0;
    viewModel.hour = clock.Hour();
    viewModel.minute = clock.Minute();
    viewModel.second = clock.Second();
    viewModel.timeScale = static_cast<float>(clock.TimeScale());
    viewModel.earthRotationMode = static_cast<int>(refs_.world.solarOverrides.earthRotationMode);
    viewModel.earthRevolutionMode = static_cast<int>(refs_.world.solarOverrides.earthRevolutionMode);
    viewModel.layoutSaveDirectory = refs_.userSettings.layoutSaveDirectory;
    viewModel.pluginRoots = refs_.userSettings.pluginRoots;
    viewModel.fullscreen = refs_.userSettings.fullscreen;
    viewModel.audioOutputDevice = refs_.userSettings.audioOutputDeviceName;
    viewModel.audioMasterVolume = refs_.userSettings.audioMasterVolume;
    viewModel.audioTrainVolume = refs_.userSettings.audioTrainVolume;
    viewModel.audioEnvironmentVolume = refs_.userSettings.audioEnvironmentVolume;
    viewModel.audioEffectsVolume = refs_.userSettings.audioEffectsVolume;
    return viewModel;
}

} // namespace rs::app

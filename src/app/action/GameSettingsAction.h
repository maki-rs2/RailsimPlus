//
// GameSettingsAction.h - ゲーム設定のアプリケーション操作
//
#pragma once

#include <string>
#include <vector>

#include "../../world/time/WorldRefs.h"
#include "../locale/Localization.h"
#include "../settings/UserSettings.h"

namespace rs::sim {
class AudioEngine;
}

namespace rs::app::input {
class InputSource;
}

namespace rs::app {

class App;
class CameraController;
class MainWindow;
struct LayoutDocumentState;
class UpdateChecker;

struct GameSettingsRefs {
    App& app;
    CameraController& camera;
    rs::world::WorldRefs world;
    Localization& localization;
    UserSettingsData& userSettings;
    rs::sim::AudioEngine& audioEngine;
    LayoutDocumentState& document;
    UpdateChecker* updateChecker = nullptr;
    // 全画面表示の切り替え先。nullptrなら設定の保存だけ行う。
    MainWindow* mainWindow = nullptr;
    // キー再割当の反映先。nullptrなら設定の保存だけ行う。
    input::InputSource* inputSource = nullptr;
};

struct GameSettingsViewModel {
    Language language = Language::Japanese;
    // UIテーマ名(ユーザー設定の値そのまま。空や不明な名前の解決は画面側)。
    std::string uiTheme;
    int year = 0;
    int month = 0;
    int day = 0;
    int dayOfWeek = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    float timeScale = 0.0f;
    int earthRotationMode = 0;
    std::string layoutSaveDirectory;
    std::vector<std::string> pluginRoots;
    bool fullscreen = false;
    std::string audioOutputDevice;
    float audioMasterVolume = 1.0f;
    float audioTrainVolume = 1.0f;
    float audioEnvironmentVolume = 1.0f;
};

class GameSettingsAction {
public:
    explicit GameSettingsAction(const GameSettingsRefs& refs);

    void SetLanguage(Language language);
    // UIテーマ名を設定へ保存する。画面への反映は、RmlUiIntegration::SetUiThemeが設定の値を見て行う。
    void SetUiTheme(const std::string& themeName);
    void SetUpdateCheckEnabled(bool enabled);
    void CheckForUpdates();
    void OpenUpdateDownloadPage();
    void ResetCamera();
    void SetTimeScale(float scale);
    // 日付と時刻(時・分。秒は0)を設定する。日付が存在するかは呼び出し側が確かめておくこと。
    void SetDateTime(const rs::world::DateParts& date, int hour, int minute);
    // 全画面表示の設定を保存し、ウィンドウへ反映する。同じ値なら何もしない。
    void SetFullscreen(bool enabled);
    void SetEarthRotationMode(int mode);
    void SetLayoutSaveDirectory(const std::string& directoryUtf8);
    // プラグインルートを足す(複数登録。同じフォルダや空はfalseで何もしない)。削除・並べ替えはPluginRootAction。
    bool AddPluginRoot(const std::string& rootUtf8);
    void SetAudioOutputDevice(const std::string& deviceNameUtf8);
    std::vector<std::string> EnumerateAudioOutputDevices() const;
    // スライダーのドラッグ中は反映だけ行い、保存はドラッグ終了時のCommitで行う。
    void SetAudioMasterVolume(float volume);
    void CommitAudioMasterVolume();
    void SetAudioTrainVolume(float volume);
    void SetAudioEnvironmentVolume(float volume);
    void CommitAudioCategoryVolumes();
    GameSettingsViewModel ViewModel() const;

private:
    GameSettingsRefs refs_;
};

} // namespace rs::app

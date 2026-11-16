#pragma once

#include <string>
#include <vector>

namespace rs::app {

struct GameSettingsChoice {
    std::string key;
    std::string label;
    bool operator==(const GameSettingsChoice&) const = default;
};

// 画面の固定文言(言語を切り替えると変わる)。
struct GameSettingsTexts {
    std::string title;
    std::string language;
    std::string theme;
    std::string masterVolume;
    std::string trainVolume;
    std::string environmentVolume;
    std::string outputDevice;
    std::string refreshDevices;
    std::string pluginRoots;
    std::string manageRoots;
    std::string layoutSaveDirectory;
    std::string browse;
    std::string resetToDefault;
    std::string camera;
    std::string resetCamera;
    std::string dateTime;
    std::string timeScale;
    std::string fpsDisplay;
    std::string effectsVolume;
    std::string version;
    std::string copyright;
    std::string openDownloadPage;
    std::string updateCheckEnabled;
    std::string checkNow;
    bool operator==(const GameSettingsTexts&) const = default;
};

namespace workspace {
// 一般設定と、保持している時間設定の表示用の値。
struct GameSettingsModel {
    std::string tab = "general";
    std::string appVersion;
    std::string updateStatus;
    bool updateAvailable = false;
    bool updateCheckEnabled = true;
    bool canCheckUpdate = false;
    std::vector<GameSettingsChoice> tabs;
    GameSettingsTexts texts;
    std::string language;
    std::vector<GameSettingsChoice> languages;
    std::string theme;
    std::vector<GameSettingsChoice> themes;
    int volumePercent = 100;
    int trainVolumePercent = 100;
    int environmentVolumePercent = 100;
    int effectsVolumePercent = 100;
    std::string audioDevice = "0"; // 選択位置(audioDeviceLabelsの添字)の文字
    std::vector<std::string> audioDeviceLabels;
    std::string pluginRoots;
    std::string layoutSaveDirectory;
    bool layoutSaveDirectoryIsDefault = true;
    bool picking = false;
    std::string framerate;
    std::string dateTime;
    std::string timeScale;
};

struct GameSettingsPresentation {
    std::string fullscreen_;
    std::string label_;
    std::string note_;
    std::vector<GameSettingsChoice> choices_;
};

GameSettingsPresentation MakeGameSettingsPlaceholder();
} // namespace workspace
} // namespace rs::app

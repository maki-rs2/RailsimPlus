#include "GameSettingsScreenModel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>

#include "MissingPluginViewModel.h"
#include "../../world/time/SolarModeNames.h"

namespace rs::app {
namespace {

constexpr std::array<Language, 2> kLanguages{Language::Japanese, Language::English};
constexpr std::array<const char*, 2> kLanguageTextKeys{"game_settings.language.japanese",
                                                       "game_settings.language.english"};

// タブの名前と文言のキー。並びが画面のタブの並びになる。
struct TabEntry {
    const char* key;
    const char* textKey;
};
constexpr std::array<TabEntry, 5> kTabs{{
    {"general", "game_settings.tab.general"},
    {"sound", "game_settings.tab.sound"},
    {"render", "game_settings.tab.render"},
    {"input", "game_settings.tab.input"},
    {"about", "game_settings.tab.about"},
}};

// 自転の文言のキー。rs::world::kEarthRotationKeysと同じ並び。
constexpr std::array<const char*, 3> kEarthRotationTextKeys{"game_settings.earth_rotation.auto",
                                                            "game_settings.earth_rotation.day",
                                                            "game_settings.earth_rotation.night"};
static_assert(kEarthRotationTextKeys.size() == rs::world::kEarthRotationKeys.size());

constexpr std::array<const char*, 7> kWeekdayTextKeys{
    "game_settings.weekday.0", "game_settings.weekday.1", "game_settings.weekday.2", "game_settings.weekday.3",
    "game_settings.weekday.4", "game_settings.weekday.5", "game_settings.weekday.6"};

std::string DefaultPathText(const std::string& folder, const Localization& localization) {
    return ExpandMissingPluginText(localization.Text("game_settings.default_path"), 0, folder);
}

} // namespace

GameSettingsTexts BuildGameSettingsTexts(const Localization& localization) {
    GameSettingsTexts texts;
    texts.title = localization.Text("game_settings.title");
    texts.language = localization.Text("game_settings.language");
    texts.theme = localization.Text("game_settings.theme");
    texts.masterVolume = localization.Text("game_settings.master_volume");
    texts.trainVolume = localization.Text("game_settings.train_volume");
    texts.environmentVolume = localization.Text("game_settings.environment_volume");
    texts.outputDevice = localization.Text("game_settings.output_device");
    texts.refreshDevices = localization.Text("game_settings.refresh_devices");
    texts.pluginRoots = localization.Text("game_settings.plugin_roots");
    texts.manageRoots = localization.Text("game_settings.manage_roots");
    texts.layoutSaveDirectory = localization.Text("game_settings.layout_save_directory");
    texts.browse = localization.Text("game_settings.browse");
    texts.resetToDefault = localization.Text("game_settings.reset_to_default");
    texts.camera = localization.Text("game_settings.camera");
    texts.resetCamera = localization.Text("game_settings.reset_camera");
    texts.dateTime = localization.Text("game_settings.date_time");
    texts.timeScale = localization.Text("game_settings.time_scale");
    texts.earthRotation = localization.Text("game_settings.earth_rotation");
    texts.fpsDisplay = localization.Text("game_settings.fps_display");
    texts.effectsVolume = localization.Text("game_settings.effects_volume");
    texts.version = localization.Text("game_settings.about.version");
    texts.copyright = localization.Text("game_settings.about.copyright");
    texts.openDownloadPage = localization.Text("game_settings.about.open_download_page");
    texts.updateCheckEnabled = localization.Text("game_settings.about.update_check_enabled");
    texts.checkNow = localization.Text("game_settings.about.check_now");
    return texts;
}

std::vector<GameSettingsChoice> BuildGameSettingsTabChoices(const Localization& localization) {
    std::vector<GameSettingsChoice> choices;
    for (const TabEntry& tab : kTabs) choices.push_back({tab.key, localization.Text(tab.textKey)});
    return choices;
}

std::vector<GameSettingsChoice> BuildLanguageChoices(const Localization& localization) {
    std::vector<GameSettingsChoice> choices;
    for (std::size_t i = 0; i < kLanguages.size(); ++i) {
        choices.push_back({LanguageCode(kLanguages[i]), localization.Text(kLanguageTextKeys[i])});
    }
    return choices;
}

std::vector<GameSettingsChoice> BuildUiThemeChoices(const std::vector<std::string>& themeNames,
                                                    const Localization& localization) {
    std::vector<GameSettingsChoice> choices;
    for (const std::string& name : themeNames) {
        std::string textKey = "game_settings.theme." + name;
        std::replace(textKey.begin(), textKey.end(), '-', '_');
        std::string label = localization.Text(textKey);
        // 翻訳が無いときTextはキー自身を返す。その場合はテーマ名を出す。
        choices.push_back({name, label == textKey ? name : std::move(label)});
    }
    return choices;
}

std::vector<GameSettingsChoice> BuildEarthRotationChoices(const Localization& localization) {
    std::vector<GameSettingsChoice> choices;
    for (std::size_t i = 0; i < kEarthRotationTextKeys.size(); ++i) {
        choices.push_back({std::string(rs::world::kEarthRotationKeys[i]), localization.Text(kEarthRotationTextKeys[i])});
    }
    return choices;
}

bool FindLanguageByKey(const std::string& key, Language& out) {
    for (const Language language : kLanguages) {
        if (key == LanguageCode(language)) {
            out = language;
            return true;
        }
    }
    return false;
}

std::string FormatGameDateTime(const GameSettingsViewModel& viewModel, const Localization& localization) {
    const int dayOfWeek = std::clamp(viewModel.dayOfWeek, 0, static_cast<int>(kWeekdayTextKeys.size()) - 1);
    return std::format("{:04d}-{:02d}-{:02d}({}) {:02d}:{:02d}:{:02d}", viewModel.year, viewModel.month, viewModel.day,
                       localization.Text(kWeekdayTextKeys[static_cast<std::size_t>(dayOfWeek)]), viewModel.hour,
                       viewModel.minute, viewModel.second);
}

std::string FormatFramerate(float framesPerSecond) {
    const float milliseconds = framesPerSecond > 0.0f ? 1000.0f / framesPerSecond : 0.0f;
    return std::format("{:.1f} fps ({:.2f} ms)", framesPerSecond, milliseconds);
}

int VolumeToPercent(float volume) {
    return static_cast<int>(std::lround(UserSettingsData::ClampAudioVolume(volume) *
                                       UserSettingsData::kVolumePercentScale));
}

float PercentToVolume(float percent) {
    return UserSettingsData::ClampAudioVolume(percent / UserSettingsData::kVolumePercentScale);
}

std::string FormatTimeScale(float scale) {
    return std::format("{:.1f}", scale);
}

std::vector<std::string> BuildAudioDeviceNames(const std::vector<std::string>& enumerated,
                                               const std::string& currentDevice) {
    std::vector<std::string> names;
    names.push_back(std::string());
    for (const std::string& name : enumerated) {
        if (!name.empty()) names.push_back(name);
    }
    if (!currentDevice.empty() && std::find(names.begin(), names.end(), currentDevice) == names.end()) {
        names.push_back(currentDevice);
    }
    return names;
}

std::vector<std::string> BuildAudioDeviceLabels(const std::vector<std::string>& names,
                                                const Localization& localization) {
    std::vector<std::string> labels;
    labels.reserve(names.size());
    for (const std::string& name : names) {
        labels.push_back(name.empty() ? localization.Text("game_settings.system_default_device") : name);
    }
    return labels;
}

int AudioDeviceSelectedIndex(const std::vector<std::string>& names, const std::string& currentDevice) {
    const auto found = std::find(names.begin(), names.end(), currentDevice);
    return found == names.end() ? 0 : static_cast<int>(found - names.begin());
}

std::string FormatPluginRootsSummary(const std::vector<std::string>& roots, const std::string& defaultFolder,
                                     const Localization& localization) {
    if (roots.empty()) return DefaultPathText(defaultFolder, localization);
    if (roots.size() == 1) return roots.front();
    return ExpandMissingPluginText(localization.Text("game_settings.plugin_roots_count"), roots.size(), {});
}

LayoutSaveDirectoryDisplay FormatLayoutSaveDirectory(const std::string& directory, const std::string& defaultFolder,
                                                     const Localization& localization) {
    if (directory.empty()) return {DefaultPathText(defaultFolder, localization), true};
    return {directory, false};
}

} // namespace rs::app

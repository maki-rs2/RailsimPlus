//
// UserSettings.cpp - ユーザープロファイル設定のJSON永続化。
//
#include "UserSettings.h"

#include <cstdlib>
#include <fstream>
#include <limits>
#include <algorithm>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "../../platform/file/AtomicFileWrite.h"
#include "../../platform/file/DirectoryWritable.h"
#include "../../platform/file/ExecutableDirectory.h"
#include "../../platform/file/UserDataDirectory.h"
#include "../../platform/text/Encoding.h"
#include "../../render/postprocess/AoQuality.h"
#include "../../render/shadow/ShadowDistance.h"
#include "../../render/shadow/ShadowQuality.h"
#include "FpsLimitChoices.h"

namespace rs::app {
namespace {

constexpr wchar_t kSettingsFileName[] = L"settings.dat";

Language LanguageFromName(const std::string& name) {
    if (name == "ja") return Language::Japanese;
    return Language::English;
}

} // namespace

// 設定は、実行ファイルの隣に置いてフォルダごと持ち運べるようにする。次の場合は %LOCALAPPDATA% を使う:
// すでにそちらに設定がある(今までの利用者の設定を引き継ぐ)/ 実行ファイルの隣に書き込めない。
std::filesystem::path UserSettings::DefaultPath() {
    const std::filesystem::path executableDirectory = ExecutableDirectory();
    const std::filesystem::path portablePath = executableDirectory / kSettingsFileName;
    const std::filesystem::path userDataPath = rs::platform_file::UserDataDirectory() / kSettingsFileName;
    std::error_code error;
    if (std::filesystem::exists(portablePath, error)) return portablePath;
    if (std::filesystem::exists(userDataPath, error)) return userDataPath;
    if (rs::platform_file::IsDirectoryWritable(executableDirectory)) return portablePath;
    return userDataPath;
}

// 既定の保存先は、実行ファイルの隣の saves。アプリのフォルダごと持ち運べるようにする。
std::filesystem::path UserSettings::DefaultLayoutSaveDirectory() {
    return ExecutableDirectory() / L"saves";
}

std::filesystem::path UserSettings::ExecutableDirectory() {
    return rs::platform_file::ExecutableDirectory();
}

UserSettingsData UserSettings::Load(const std::filesystem::path& path) {
    UserSettingsData data;
    data.sourcePath = path;
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open()) return data;

    try {
        const nlohmann::json document = nlohmann::json::parse(stream);
        if (document.is_object()) data.loadedDocument = document;
        const auto updateCheckEnabled = document.find("updateCheckEnabled");
        if (updateCheckEnabled != document.end() && updateCheckEnabled->is_boolean()) {
            data.updateCheckEnabled = updateCheckEnabled->get<bool>();
        }
        const auto lastUpdateCheckTime = document.find("lastUpdateCheckTime");
        if (lastUpdateCheckTime != document.end() && lastUpdateCheckTime->is_number_integer() &&
            (!lastUpdateCheckTime->is_number_unsigned() ||
             lastUpdateCheckTime->get<std::uint64_t>() <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))) {
            data.lastUpdateCheckTime = std::max(std::int64_t{0}, lastUpdateCheckTime->get<std::int64_t>());
        }
        const auto dismissedUpdateVersion = document.find("dismissedUpdateVersion");
        if (dismissedUpdateVersion != document.end() && dismissedUpdateVersion->is_string()) {
            data.dismissedUpdateVersion = dismissedUpdateVersion->get<std::string>();
        }
        const nlohmann::json::const_iterator language = document.find("language");
        if (language != document.end() && language->is_string()) {
            data.language = LanguageFromName(language->get<std::string>());
        }
        const nlohmann::json::const_iterator pluginRoots = document.find("pluginRoots");
        if (pluginRoots != document.end() && pluginRoots->is_array()) {
            for (const nlohmann::json& root : *pluginRoots) {
                if (root.is_string()) data.pluginRoots.push_back(root.get<std::string>());
            }
        }
        const nlohmann::json::const_iterator favoritePlugins = document.find("favoritePlugins");
        if (favoritePlugins != document.end() && favoritePlugins->is_array()) {
            for (const nlohmann::json& favorite : *favoritePlugins) {
                if (favorite.is_string()) data.favoritePlugins.push_back(favorite.get<std::string>());
            }
        }
        const nlohmann::json::const_iterator defaultPlugins = document.find("defaultPlugins");
        if (defaultPlugins != document.end() && defaultPlugins->is_object()) {
            for (const auto& [kind, relativePath] : defaultPlugins->items()) {
                if (relativePath.is_string()) data.defaultPlugins[kind] = relativePath.get<std::string>();
            }
        }
        const nlohmann::json::const_iterator browserCardSize = document.find("browserCardSize");
        if (browserCardSize != document.end() && browserCardSize->is_string()) {
            // 読めない値は既定(中)のまま。
            data.browserCardSize = plugin_browser::FindBrowserCardSizeByKey(browserCardSize->get<std::string>())
                                       .value_or(plugin_browser::kDefaultBrowserCardSize);
        }
        const nlohmann::json::const_iterator uiTheme = document.find("uiTheme");
        if (uiTheme != document.end() && uiTheme->is_string()) {
            data.uiTheme = uiTheme->get<std::string>(); // 読めない名前は使う側で既定になる
        }
        const nlohmann::json::const_iterator layoutSaveDirectory = document.find("layoutSaveDirectory");
        if (layoutSaveDirectory != document.end() && layoutSaveDirectory->is_string()) {
            data.layoutSaveDirectory = layoutSaveDirectory->get<std::string>();
        }
        const nlohmann::json::const_iterator recentLayouts = document.find("recentLayouts");
        if (recentLayouts != document.end() && recentLayouts->is_array()) {
            for (const nlohmann::json& recent : *recentLayouts) {
                if (recent.is_string()) data.recentLayouts.push_back(recent.get<std::string>());
            }
        }
        const nlohmann::json::const_iterator autoRecoverEnabled = document.find("autoRecoverEnabled");
        if (autoRecoverEnabled != document.end() && autoRecoverEnabled->is_boolean()) {
            data.autoRecoverEnabled = autoRecoverEnabled->get<bool>();
        }
        const nlohmann::json::const_iterator autoRecoverIntervalMinutes =
            document.find("autoRecoverIntervalMinutes");
        if (autoRecoverIntervalMinutes != document.end() && autoRecoverIntervalMinutes->is_number_integer()) {
            const int value = autoRecoverIntervalMinutes->get<int>();
            if (value > 0 && value <= 24 * 60) data.autoRecoverIntervalMinutes = value;
        }
        const nlohmann::json::const_iterator audioOutputDeviceName = document.find("audioOutputDeviceName");
        if (audioOutputDeviceName != document.end() && audioOutputDeviceName->is_string()) {
            data.audioOutputDeviceName = audioOutputDeviceName->get<std::string>();
        }
        const nlohmann::json::const_iterator audioMasterVolume = document.find("audioMasterVolume");
        if (audioMasterVolume != document.end() && audioMasterVolume->is_number()) {
            data.audioMasterVolume = UserSettingsData::ClampAudioVolume(audioMasterVolume->get<float>());
        }
        const nlohmann::json::const_iterator audioTrainVolume = document.find("audioTrainVolume");
        if (audioTrainVolume != document.end() && audioTrainVolume->is_number()) {
            data.audioTrainVolume = UserSettingsData::ClampAudioVolume(audioTrainVolume->get<float>());
        }
        const nlohmann::json::const_iterator audioEnvironmentVolume = document.find("audioEnvironmentVolume");
        if (audioEnvironmentVolume != document.end() && audioEnvironmentVolume->is_number()) {
            data.audioEnvironmentVolume = UserSettingsData::ClampAudioVolume(audioEnvironmentVolume->get<float>());
        }
        const nlohmann::json::const_iterator audioEffectsVolume = document.find("audioEffectsVolume");
        if (audioEffectsVolume != document.end() && audioEffectsVolume->is_number()) {
            data.audioEffectsVolume = UserSettingsData::ClampAudioVolume(audioEffectsVolume->get<float>());
        }
        const nlohmann::json::const_iterator atmosphericPerspective = document.find("atmosphericPerspective");
        if (atmosphericPerspective != document.end() && atmosphericPerspective->is_boolean()) {
            data.atmosphericPerspective = atmosphericPerspective->get<bool>();
        }
        const nlohmann::json::const_iterator heightFog = document.find("heightFog");
        if (heightFog != document.end() && heightFog->is_boolean()) {
            data.heightFog = heightFog->get<bool>();
        }
        const nlohmann::json::const_iterator tonemap = document.find("tonemap");
        if (tonemap != document.end() && tonemap->is_string()) {
            data.tonemapMode = tonemap->get<std::string>(); // 読めない名前は使う側で既定になる
        }
        const nlohmann::json::const_iterator shadowQuality = document.find("shadowQuality");
        if (shadowQuality != document.end() && shadowQuality->is_string()) {
            // 読めない値は既定(低)へそろえる。
            data.shadowQuality = rs::render::ShadowQualityKey(
                rs::render::ShadowQualityFromKey(shadowQuality->get<std::string>()));
        }
        const nlohmann::json::const_iterator shadowDistance = document.find("shadowDistance");
        if (shadowDistance != document.end() && shadowDistance->is_string()) {
            // 読めない値は既定(標準)へそろえる。
            data.shadowDistance = rs::render::ShadowDistanceKey(
                rs::render::ShadowDistanceFromKey(shadowDistance->get<std::string>()));
        }
        struct SwitchField {
            const char* key;
            bool* field;
        };
        const SwitchField lensFlareFields[] = {{"sunLensFlare", &data.sunLensFlare},
                                               {"sunWhiteout", &data.sunWhiteout},
                                               {"miscLensFlare", &data.miscLensFlare},
                                               {"miscParticle", &data.miscParticle}};
        for (const SwitchField& entry : lensFlareFields) {
            const nlohmann::json::const_iterator found = document.find(entry.key);
            if (found != document.end() && found->is_boolean()) *entry.field = found->get<bool>();
        }
        const nlohmann::json::const_iterator bloom = document.find("bloom");
        if (bloom != document.end() && bloom->is_boolean()) data.bloom = bloom->get<bool>();
        const nlohmann::json::const_iterator motionBlur = document.find("motionBlur");
        if (motionBlur != document.end() && motionBlur->is_boolean()) data.motionBlur = motionBlur->get<bool>();
        const nlohmann::json::const_iterator volumetricFog = document.find("volumetricFog");
        if (volumetricFog != document.end() && volumetricFog->is_boolean()) data.volumetricFog = volumetricFog->get<bool>();
        const nlohmann::json::const_iterator finderGrid = document.find("cameraFinderGrid");
        if (finderGrid != document.end() && finderGrid->is_boolean()) data.cameraFinderGrid = finderGrid->get<bool>();
        const nlohmann::json::const_iterator finderHorizon = document.find("cameraFinderHorizon");
        if (finderHorizon != document.end() && finderHorizon->is_boolean()) data.cameraFinderHorizon = finderHorizon->get<bool>();
        const nlohmann::json::const_iterator viewOperation = document.find("viewOperation");
        if (viewOperation != document.end() && viewOperation->is_string()) {
            // 読めない値は既定(WASD)へそろえる。
            data.viewOperation = ViewOperationModeFromKey(viewOperation->get<std::string>());
        }
        const nlohmann::json::const_iterator ambientOcclusion = document.find("ambientOcclusion");
        if (ambientOcclusion != document.end() && ambientOcclusion->is_string()) {
            // 読めない値は既定(中)へそろえる。
            data.ambientOcclusion = rs::render::AoQualityKey(
                rs::render::AoQualityFromKey(ambientOcclusion->get<std::string>()));
        }
        const nlohmann::json::const_iterator antiAliasing = document.find("aa");
        if (antiAliasing != document.end() && antiAliasing->is_string()) {
            // 読めない値は既定(FXAA)へそろえる。
            data.antiAliasing = rs::render::AntiAliasingKey(
                rs::render::AntiAliasingFromKey(antiAliasing->get<std::string>()));
        }
        const nlohmann::json::const_iterator vsync = document.find("vsync");
        if (vsync != document.end() && vsync->is_boolean()) {
            data.vsync = vsync->get<bool>();
        }
        const nlohmann::json::const_iterator fullscreen = document.find("fullscreen");
        if (fullscreen != document.end() && fullscreen->is_boolean()) {
            data.fullscreen = fullscreen->get<bool>();
        }
        const nlohmann::json::const_iterator keyBindings = document.find("keyBindings");
        if (keyBindings != document.end() && keyBindings->is_string()) {
            data.keyBindings = keyBindings->get<std::string>(); // 行ごとの検証は割当表の組み立て側
        }
        const nlohmann::json::const_iterator fpsLimit = document.find("fpsLimit");
        if (fpsLimit != document.end() && fpsLimit->is_number_integer()) {
            // 選択肢に無い値は無制限へそろえる。
            data.fpsLimit = NormalizeFpsLimit(fpsLimit->get<int>());
        }
    } catch (const nlohmann::json::exception& exception) {
        // 既定値で起動し、次の保存で上書きされないよう壊れたファイルを退避する。
        stream.close();
        std::filesystem::path backup = path;
        backup += ".broken";
        std::error_code copyError;
        std::filesystem::copy_file(path, backup, std::filesystem::copy_options::overwrite_existing, copyError);
        spdlog::warn("Failed to load user settings '{}': {} (backup: {})",
                     rs::platform_text::WideToUtf8(path.wstring()), exception.what(),
                     copyError ? std::string("failed") : rs::platform_text::WideToUtf8(backup.wstring()));
        UserSettingsData defaults;
        defaults.sourcePath = path;
        return defaults;
    }
    return data;
}

bool UserSettings::Save(const std::filesystem::path& path, const UserSettingsData& data) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        spdlog::warn("Failed to create user settings directory '{}': {}",
                     rs::platform_text::WideToUtf8(path.parent_path().wstring()), error.message());
        return false;
    }

    const nlohmann::json known = {{"language", LanguageCode(data.language)},
                                     {"pluginRoots", data.pluginRoots},
                                     {"favoritePlugins", data.favoritePlugins},
                                     {"defaultPlugins", data.defaultPlugins},
                                     {"browserCardSize", plugin_browser::GetBrowserCardSize(data.browserCardSize).key},
                                     {"uiTheme", data.uiTheme},
                                     {"layoutSaveDirectory", data.layoutSaveDirectory},
                                     {"recentLayouts", data.recentLayouts},
                                     {"autoRecoverEnabled", data.autoRecoverEnabled},
                                     {"autoRecoverIntervalMinutes", std::clamp(data.autoRecoverIntervalMinutes, 1, 24 * 60)},
                                     {"audioOutputDeviceName", data.audioOutputDeviceName},
                                     {"audioMasterVolume", UserSettingsData::ClampAudioVolume(data.audioMasterVolume)},
                                     {"audioTrainVolume", UserSettingsData::ClampAudioVolume(data.audioTrainVolume)},
                                     {"audioEnvironmentVolume", UserSettingsData::ClampAudioVolume(data.audioEnvironmentVolume)},
                                     {"audioEffectsVolume", UserSettingsData::ClampAudioVolume(data.audioEffectsVolume)},
                                     {"atmosphericPerspective", data.atmosphericPerspective},
                                     {"heightFog", data.heightFog},
                                     {"tonemap", data.tonemapMode},
                                     {"shadowQuality", data.shadowQuality},
                                     {"shadowDistance", data.shadowDistance},
                                     {"sunLensFlare", data.sunLensFlare},
                                     {"sunWhiteout", data.sunWhiteout},
                                     {"miscLensFlare", data.miscLensFlare},
                                     {"miscParticle", data.miscParticle},
                                     {"bloom", data.bloom},
                                     {"motionBlur", data.motionBlur},
                                     {"volumetricFog", data.volumetricFog},
                                     {"cameraFinderGrid", data.cameraFinderGrid},
                                     {"cameraFinderHorizon", data.cameraFinderHorizon},
                                     {"viewOperation", ViewOperationModeKey(data.viewOperation)},
                                     {"aa", data.antiAliasing},
                                     {"ambientOcclusion", data.ambientOcclusion},
                                     {"vsync", data.vsync},
                                     {"fullscreen", data.fullscreen},
                                     {"fpsLimit", NormalizeFpsLimit(data.fpsLimit)}};
    // 知らないキーは読み込んだときのまま残し、知っているキーだけを今の値で上書きする。
    nlohmann::json document = data.loadedDocument.is_object() ? data.loadedDocument : nlohmann::json::object();
    document.update(known);
    // 既定のままなら項目を書かない(読み込んだ文書に残っていた古い値も消す)。
    if (data.keyBindings.empty()) document.erase("keyBindings");
    else document["keyBindings"] = data.keyBindings;
    document["updateCheckEnabled"] = data.updateCheckEnabled;
    document["lastUpdateCheckTime"] = data.lastUpdateCheckTime;
    document["dismissedUpdateVersion"] = data.dismissedUpdateVersion;
    std::string writeError;
    if (!rs::platform_file::WriteFileAtomically(path, document.dump(2), writeError)) {
        spdlog::warn("Failed to save user settings '{}': {}",
                     rs::platform_text::WideToUtf8(path.wstring()), writeError);
        return false;
    }
    return true;
}

rs::render::LensFlareSwitches LensFlareSwitchesFrom(const UserSettingsData& data) {
    return {data.sunLensFlare, data.sunWhiteout, data.miscLensFlare};
}

rs::render::PostProcessSettings PostProcessSettingsFrom(const UserSettingsData& data) {
    rs::render::PostProcessSettings settings;
    settings.bloom = data.bloom;
    settings.motionBlur = data.motionBlur;
    settings.volumetricFog = data.volumetricFog;
    settings.aa = rs::render::AntiAliasingFromKey(data.antiAliasing);
    return settings;
}

bool SaveUserSettingsOrWarn(const UserSettingsData& data, const char* failureMessage) {
    const bool saved = UserSettings::Save(data.sourcePath, data);
    if (!saved) spdlog::warn("{}", failureMessage);
    return saved;
}

} // namespace rs::app

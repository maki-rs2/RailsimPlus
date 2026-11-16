#include "ActiveEnv.h"

#include "../../platform/EnvironmentVariable.h"
#include <cmath>
#include <cstdlib>
#include <cwctype>
#include <filesystem>

#include <spdlog/spdlog.h>

#include "LayoutSession.h"
#include "../../platform/text/Encoding.h"
#include "../../plugins/layout/Rs2Import.h"
#include "../../plugins/load/EnvPluginLoad.h"

namespace rs::app {

namespace {

constexpr float kByteMax = 255.0f;

unsigned ToByte(float channel) {
    return static_cast<unsigned>(std::lround(channel * kByteMax));
}

// ログ用に #AARRGGBB の文字列にする。
std::string FormatColorArgb(const rs::legacy_pi::PiColor& color) {
    return fmt::format("#{:02x}{:02x}{:02x}{:02x}", ToByte(color.a), ToByte(color.r), ToByte(color.g),
                       ToByte(color.b));
}

void LogEnv(const std::string& name, const rs::legacy_pi::EnvDefinition& env) {
    const std::string latitude = env.hasLatitude ? fmt::format("{}", env.latitudeDeg) : std::string("none");
    spdlog::info("[env] name={} latitude={} landscape={} envMap={} lightingSets={} shadowColor={}", name,
                 latitude, env.landscapeModelFileName.empty() ? "(none)" : env.landscapeModelFileName,
                 env.envMapTexFileName.empty() ? "(none)" : env.envMapTexFileName, env.lightingSets.size(),
                 env.hasShadowColor ? FormatColorArgb(env.shadowColor) : std::string("none"));
}

// EnvMapTexFileNameがキューブ(.dds)か。2Dの.png(大半のEnv)は焼き済みIBLを持っていても
// プラグインの空にはせず常に標準の空にする(判別は拡張子で行う)。
bool IsCubeBackground(const std::string& pathUtf8) {
    std::wstring extension = std::filesystem::path(rs::platform_text::Utf8ToWide(pathUtf8)).extension().wstring();
    for (wchar_t& c : extension) c = static_cast<wchar_t>(std::towlower(c));
    return extension == L".dds";
}

} // namespace

void ActiveEnv::Update(const LayoutSession& session, const RootCandidatesFn& rootCandidates) {
    const LayoutSceneCache* scene =
        session.activeIndex < session.scenes.size() ? &session.scenes[session.activeIndex] : nullptr;
    std::string name = scene != nullptr ? scene->envPlugin : std::string();
    std::string folderUtf8 = scene != nullptr ? scene->envFolderUtf8 : std::string();

    // 開発用の上書き(起動後に1回だけ読み、解決も初回に1回だけ)。上書き > Sceneのenv名。
    if (!overrideRead_) {
        overrideRead_ = true;
        const char* value = rs::platform::ReadEnvironmentVariable("RS_ENV_OVERRIDE");
        if (value != nullptr) overrideName_ = value;
    }
    if (!overrideName_.empty() && !overrideResolved_) {
        overrideResolved_ = true;
        overrideFolderUtf8_ = ResolveEnvPluginFolder(rootCandidates ? rootCandidates() : std::vector<std::string>(),
                                                     overrideName_);
        if (overrideFolderUtf8_.empty()) {
            spdlog::warn("[env] RS_ENV_OVERRIDE '{}' was not found under the plugin roots; ignoring the override",
                         overrideName_);
            overrideName_.clear();
        }
    }
    if (!overrideName_.empty()) {
        name = overrideName_;
        folderUtf8 = overrideFolderUtf8_;
    }

    const std::string key = name + '\n' + folderUtf8;
    if (key == key_) return;
    key_ = key;
    name_ = name;
    folderUtf8_ = folderUtf8;
    ++generation_;
    definition_.reset();
    if (name.empty()) return;  // Env未指定: 環境プリセットのまま
    if (folderUtf8.empty()) {
        spdlog::warn("[env] EnvPlugin '{}' was not found under the plugin roots; using the preset", name);
        return;
    }

    const rs::legacy_plugin::EnvPluginLoadResult loaded =
        rs::legacy_plugin::LoadEnvPlugin(rs::platform_text::Utf8ToWide(folderUtf8));
    if (loaded.noDefinition) {
        spdlog::info("[env] name={} has no definition file; using the preset", name);
        return;
    }
    for (const rs::legacy_pi::Diagnostic& diagnostic : loaded.diagnostics) {
        spdlog::warn("[env] {} ({}): {}", name, diagnostic.severity == rs::legacy_pi::Severity::Error ? "error" : "warn",
                     diagnostic.message);
    }
    if (!loaded.ok) {
        spdlog::warn("[env] EnvPlugin '{}' could not be read; using the preset", name);
        return;
    }
    definition_ = loaded.definition;
    LogEnv(name, loaded.definition);
}

const rs::legacy_pi::EnvDefinition* ActiveEnv::Definition() const {
    return definition_.has_value() ? &definition_.value() : nullptr;
}

EnvCubePaths ResolveEnvCubePaths(const std::string& folderUtf8, const rs::legacy_pi::EnvDefinition& env) {
    EnvCubePaths paths;
    // 背景(EnvMapTexFileName)は読まない。キューブ(.dds)かどうかだけを、プラグインの空にする条件に使う。
    const std::string background = rs::legacy_plugin::ResolveEnvTexturePath(folderUtf8, env.envMapTexFileName);
    paths.diffuse = rs::legacy_plugin::ResolveEnvTexturePath(folderUtf8, env.diffuseIrradianceTexFileName);
    paths.specular = rs::legacy_plugin::ResolveEnvTexturePath(folderUtf8, env.specularIrradianceTexFileName);
    paths.brdfLut = rs::legacy_plugin::ResolveEnvTexturePath(folderUtf8, env.brdfLutTexFileName);
    if (background.empty() || paths.diffuse.empty() || paths.specular.empty() || paths.brdfLut.empty()) {
        return {};
    }
    if (!IsCubeBackground(background)) return {};
    return paths;
}

EnvCubePaths ActiveEnv::CubePaths() const {
    if (!definition_.has_value() || folderUtf8_.empty()) return {};
    return ResolveEnvCubePaths(folderUtf8_, *definition_);
}

EnvSkyBodyPaths ActiveEnv::SkyBodyPaths() const {
    if (!definition_.has_value() || folderUtf8_.empty()) return {};
    const rs::legacy_pi::EnvDefinition& env = *definition_;
    EnvSkyBodyPaths paths;
    paths.sunModel = rs::legacy_plugin::ResolveEnvTexturePath(folderUtf8_, env.sunModelFileName);
    paths.sunScale = env.sunModelScale;
    paths.moonModel = rs::legacy_plugin::ResolveEnvTexturePath(folderUtf8_, env.moonModelFileName);
    paths.moonScale = env.moonModelScale;
    paths.landscapeModel = rs::legacy_plugin::ResolveEnvTexturePath(folderUtf8_, env.landscapeModelFileName);
    paths.landscapeScale = env.landscapeModelScale;
    return paths;
}

std::string ActiveEnv::ResolveFilePath(const std::string& relativePathUtf8) const {
    if (!definition_.has_value() || folderUtf8_.empty()) return {};
    return rs::legacy_plugin::ResolveEnvTexturePath(folderUtf8_, relativePathUtf8);
}

double ActiveEnv::SolarLatitudeDeg(double fallbackDeg) const {
    return rs::legacy_pi::ResolveSolarLatitudeDeg(Definition(), fallbackDeg);
}

std::string ResolveEnvPluginFolder(const std::vector<std::string>& rootCandidatesUtf8,
                                   const std::string& envName) {
    if (envName.empty()) return {};
    for (const std::string& rootUtf8 : rootCandidatesUtf8) {
        // 名前→フォルダの組み立ては、Rail/Pole等のプラグイン参照と同じ規約(ルート/種別/名前)。
        const std::string folderUtf8 = rs::legacy_layout::ResolveTrackPluginFolder(rootUtf8, "Env", envName);
        std::error_code error;
        if (std::filesystem::is_directory(std::filesystem::path(rs::platform_text::Utf8ToWide(folderUtf8)), error)) {
            return folderUtf8;
        }
    }
    return {};
}

void ResolveSceneEnvFolders(std::vector<LayoutSceneCache>& scenes,
                            const std::vector<std::string>& rootCandidatesUtf8) {
    for (LayoutSceneCache& scene : scenes) {
        scene.envFolderUtf8 = ResolveEnvPluginFolder(rootCandidatesUtf8, scene.envPlugin);
    }
}

} // namespace rs::app

#include "EnvironmentPresets.h"

#include "../../core/ConfigJson.h"

#include <optional>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "EmbeddedEnvironmentPresets.h"  // ビルド時生成(CMakeLists.txt)

#include "../../platform/file/AssetPath.h"
#include "../../platform/file/FileRead.h"

namespace rs::world {

GameClock MakeInitialGameClock(const EnvironmentPreset& preset) {
    GameClock clock;
    clock.SetDateTime(FromDateParts(preset.initialDate), preset.initialSecondsOfDay);
    return clock;
}

bool EnvironmentPresets::LoadEmbedded() {
    if (ParseJsonText(reinterpret_cast<const char*>(rs::embedded::kEnvironmentPresetsJson),
                      rs::embedded::kEnvironmentPresetsJsonSize, "EnvironmentPresets")) return true;
    *this = EnvironmentPresets{};
    spdlog::warn("EnvironmentPresets: 既定値を使います");
    return false;
}

bool EnvironmentPresets::LoadFromFile(const std::string& utf8Path) {
    const std::optional<std::string> text = rs::platform_file::ReadFileText(utf8Path);
    if (!text) {
        spdlog::warn("EnvironmentPresets: ファイルを開けません: {}", utf8Path);
        return false;
    }

    return ParseJsonText(text->data(), text->size(), utf8Path);
}

bool EnvironmentPresets::ParseJsonText(const char* text, std::size_t size,
                                       const std::string& sourceLabel) {
    const std::optional<EnvironmentPresets> parsed = rs::core::TryReadConfigJson<EnvironmentPresets>(
        std::string_view(text, size), sourceLabel.c_str(), [this](const nlohmann::json& root) {
            EnvironmentPresets result = *this;
            if (root.contains("default") && root["default"].is_string())
                result.defaultName_ = root["default"].get<std::string>();

            if (!root.contains("presets") || !root["presets"].is_array())
                throw std::invalid_argument("presetsは配列である必要があります");

            for (const auto& item : root["presets"]) {
                EnvironmentPreset preset;
                preset.name = item.value("name", std::string());
                preset.latitudeDeg = item.value("latitudeDeg", preset.latitudeDeg);
                preset.axialInclinationDeg =
                    item.value("axialInclinationDeg", preset.axialInclinationDeg);
                preset.initialDate.year = item.value("initialYear", preset.initialDate.year);
                preset.initialDate.month = item.value("initialMonth", preset.initialDate.month);
                preset.initialDate.day = item.value("initialDay", preset.initialDate.day);
                preset.initialSecondsOfDay =
                    item.value("initialSecondsOfDay", preset.initialSecondsOfDay);
                preset.nightThresholdSin = item.value("nightThresholdSin", preset.nightThresholdSin);
                result.presets_.push_back(preset);
            }
            return result;
        });
    if (!parsed) return false;
    *this = *parsed;
    return true;
}

const EnvironmentPreset* EnvironmentPresets::Find(const std::string& name) const {
    for (const auto& preset : presets_)
        if (preset.name == name) return &preset;
    return nullptr;
}

const EnvironmentPreset& EnvironmentPresets::Default() const {
    static const EnvironmentPreset kFallback;
    if (!defaultName_.empty()) {
        if (const EnvironmentPreset* found = Find(defaultName_)) return *found;
    }
    if (!presets_.empty()) return presets_.front();
    return kFallback;
}

std::string ResolveEnvironmentPresetsPath() {
    return rs::platform_file::ResolveAssetPath("assets/config/environment_presets.json");
}

} // namespace rs::world

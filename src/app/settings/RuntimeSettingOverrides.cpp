#include "RuntimeSettingOverrides.h"

#include <string_view>

#include <spdlog/spdlog.h>

#include "../../render/postprocess/TonemapMath.h"

namespace rs::app {

RuntimeSettingOverrides ParseRuntimeSettingOverrides(const std::vector<std::string>& arguments) {
    RuntimeSettingOverrides result;
    for (const std::string& argument : arguments) {
        const std::size_t separator = argument.find('=');
        if (separator == std::string::npos || separator == 0 || separator + 1 >= argument.size()) {
            spdlog::warn("[startup] --setting requires key=value: '{}'", argument);
            continue;
        }
        const std::string_view key(argument.data(), separator);
        const std::string_view value(argument.data() + separator + 1, argument.size() - separator - 1);
        const auto invalid = [&]() { spdlog::warn("[startup] invalid --setting '{}'; ignoring", argument); };
        const auto parseBool = [&]() -> std::optional<bool> {
            if (value == "true" || value == "on" || value == "1") return true;
            if (value == "false" || value == "off" || value == "0") return false;
            return std::nullopt;
        };
        if (key == "aa") {
            if (value == "none" || value == "fxaa" || value == "taa") result.antiAliasing = std::string(value);
            else invalid();
        } else if (key == "tonemap") {
            bool found = false;
            for (int i = 0; i < static_cast<int>(rs::render::kTonemapModeCount); ++i) {
                const auto mode = static_cast<rs::render::TonemapMode>(i);
                if (value == rs::render::TonemapModeName(mode)) {
                    result.tonemap = std::string(value);
                    found = true;
                    break;
                }
            }
            if (!found) invalid();
        } else if (key == "skyMode") {
            if (value == "plugin") result.pluginSkyPreferred = true;
            else if (value == "standard") result.pluginSkyPreferred = false;
            else invalid();
        } else if (key == "atmosphericPerspective" || key == "heightFog" || key == "motionBlur" ||
                   key == "volumetricFog") {
            const std::optional<bool> enabled = parseBool();
            if (!enabled.has_value()) { invalid(); continue; }
            if (key == "atmosphericPerspective") result.atmosphericPerspective = *enabled;
            else if (key == "heightFog") result.heightFog = *enabled;
            else if (key == "motionBlur") result.motionBlur = *enabled;
            else result.volumetricFog = *enabled;
        } else {
            spdlog::warn("[startup] unknown --setting key '{}'; ignoring", key);
        }
    }
    return result;
}

} // namespace rs::app

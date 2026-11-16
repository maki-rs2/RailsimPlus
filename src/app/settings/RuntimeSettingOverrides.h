#pragma once

#include <optional>
#include <string>
#include <vector>

namespace rs::app {

// 描画の設定画面にある項目への起動時上書き。永続化用UserSettingsDataとは分けて保持する。
struct RuntimeSettingOverrides {
    std::optional<std::string> antiAliasing;
    std::optional<std::string> tonemap;
    std::optional<bool> pluginSkyPreferred;
    std::optional<bool> atmosphericPerspective;
    std::optional<bool> heightFog;
    std::optional<bool> motionBlur;
    std::optional<bool> volumetricFog;
};

RuntimeSettingOverrides ParseRuntimeSettingOverrides(const std::vector<std::string>& arguments);

} // namespace rs::app

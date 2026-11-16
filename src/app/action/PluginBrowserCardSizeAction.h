//
// PluginBrowserCardSizeAction.h - プラグインブラウザのカードの大きさ(大・中・小)の操作
//
// 大きさはUserSettingsData::browserCardSizeに持つ。変えたらすぐ settings.dat へ保存する。
//
#pragma once

#include "../settings/UserSettings.h"

namespace rs::app {

class PluginBrowserCardSizeAction {
public:
    explicit PluginBrowserCardSizeAction(UserSettingsData& userSettings) : userSettings_(userSettings) {}

    plugin_browser::BrowserCardSize Size() const { return userSettings_.browserCardSize; }
    // 変わるときだけ保存する。変わったらtrueを返す。
    bool Set(plugin_browser::BrowserCardSize size);

private:
    UserSettingsData& userSettings_;
};

} // namespace rs::app

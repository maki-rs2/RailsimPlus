//
// PluginRootAction.h - プラグインルート(複数)の追加・削除・並べ替え
//
// UserSettingsData::pluginRoots を編集し、変えたらすぐ settings.dat へ保存する。
//
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "../settings/UserSettings.h"

namespace rs::app {

class PluginRootAction {
public:
    explicit PluginRootAction(UserSettingsData& userSettings) : userSettings_(userSettings) {}

    const std::vector<std::string>& Roots() const { return userSettings_.pluginRoots; }
    // 空白だけ、または同じフォルダ(SamePluginPath)が既にあればfalseを返して何もしない。
    bool Add(const std::string& pathUtf8);
    // 範囲外は何もしない。
    void Remove(std::size_t index);
    // deltaは-1/+1。範囲外は何もしない。
    void Move(std::size_t index, int delta);

private:
    UserSettingsData& userSettings_;
};

} // namespace rs::app

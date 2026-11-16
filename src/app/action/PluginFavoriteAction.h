//
// PluginFavoriteAction.h - プラグインブラウザのお気に入り操作
//
// お気に入りはルートからの相対パス(PluginCatalogEntry::relativeKey、"Rail/foo"の形)で
// UserSettingsData::favoritePlugins に持つ。変えたらすぐ settings.dat へ保存する。
//
#pragma once

#include <string>
#include <vector>

#include "../settings/UserSettings.h"

namespace rs::app {

class PluginFavoriteAction {
public:
    explicit PluginFavoriteAction(UserSettingsData& userSettings) : userSettings_(userSettings) {}

    // ASCIIの大文字小文字を区別せずに比べる。
    bool IsFavorite(const std::string& relativeKey) const;
    // お気に入りなら外し、そうでなければ末尾へ足す。
    void Toggle(const std::string& relativeKey);
    const std::vector<std::string>& Favorites() const { return userSettings_.favoritePlugins; }

private:
    UserSettingsData& userSettings_;
};

} // namespace rs::app

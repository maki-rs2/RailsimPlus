#include "PluginFavoriteAction.h"

#include <algorithm>

#include "../../platform/text/AsciiCase.h"

namespace rs::app {
namespace {

std::vector<std::string>::const_iterator FindFavorite(const std::vector<std::string>& favorites,
                                                      const std::string& relativeKey) {
    return std::find_if(favorites.begin(), favorites.end(), [&relativeKey](const std::string& favorite) {
        return rs::platform_text::EqualsIgnoreAsciiCase(favorite, relativeKey);
    });
}

} // namespace

bool PluginFavoriteAction::IsFavorite(const std::string& relativeKey) const {
    return FindFavorite(userSettings_.favoritePlugins, relativeKey) != userSettings_.favoritePlugins.end();
}

void PluginFavoriteAction::Toggle(const std::string& relativeKey) {
    std::vector<std::string>& favorites = userSettings_.favoritePlugins;
    const std::vector<std::string>::const_iterator found = FindFavorite(favorites, relativeKey);
    if (found != favorites.end()) {
        favorites.erase(found);
    } else {
        favorites.push_back(relativeKey);
    }
    SaveUserSettingsOrWarn(userSettings_, "Failed to save user settings after toggling a favorite plugin.");
}

} // namespace rs::app

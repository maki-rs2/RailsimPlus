#include "PluginBrowserCardSizeAction.h"

namespace rs::app {

bool PluginBrowserCardSizeAction::Set(plugin_browser::BrowserCardSize size) {
    if (userSettings_.browserCardSize == size) return false;
    userSettings_.browserCardSize = size;
    SaveUserSettingsOrWarn(userSettings_, "Failed to save user settings after changing the plugin browser card size.");
    return true;
}

} // namespace rs::app

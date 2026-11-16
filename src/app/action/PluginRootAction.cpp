#include "PluginRootAction.h"

#include <algorithm>

#include "../plugin_session/PluginSelector.h"
#include "../../platform/text/AsciiCase.h"

namespace rs::app {

bool PluginRootAction::Add(const std::string& pathUtf8) {
    if (rs::platform_text::IsAsciiBlank(pathUtf8)) return false;
    const bool exists = std::any_of(userSettings_.pluginRoots.begin(), userSettings_.pluginRoots.end(),
                                    [&pathUtf8](const std::string& root) { return SamePluginPath(root, pathUtf8); });
    if (exists) return false;
    userSettings_.pluginRoots.push_back(pathUtf8);
    SaveUserSettingsOrWarn(userSettings_, "Failed to save user settings after adding a plugin root.");
    return true;
}

void PluginRootAction::Remove(std::size_t index) {
    if (index >= userSettings_.pluginRoots.size()) return;
    userSettings_.pluginRoots.erase(userSettings_.pluginRoots.begin() + static_cast<std::ptrdiff_t>(index));
    SaveUserSettingsOrWarn(userSettings_, "Failed to save user settings after removing a plugin root.");
}

void PluginRootAction::Move(std::size_t index, int delta) {
    if (delta != -1 && delta != 1) return;
    const std::size_t size = userSettings_.pluginRoots.size();
    if (index >= size) return;
    if (delta < 0 ? index == 0 : index + 1 >= size) return;
    const std::size_t target = delta < 0 ? index - 1 : index + 1;
    std::swap(userSettings_.pluginRoots[index], userSettings_.pluginRoots[target]);
    SaveUserSettingsOrWarn(userSettings_, "Failed to save user settings after moving a plugin root.");
}

} // namespace rs::app

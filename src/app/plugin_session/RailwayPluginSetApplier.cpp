#include "RailwayPluginSetApplier.h"

#include "PluginSelector.h"
#include "RailwayPluginSetResolve.h"

namespace rs::app {

RailwayPluginSetApplyResult ApplyRailwayPluginSetFile(const std::string& setPathUtf8,
                                                       PluginSelector& selector,
                                                       const std::string& pluginRootUtf8) {
    RailwayPluginSetApplyResult result;

    const RailwayPluginSetResolveResult resolved = ResolveRailwayPluginSetFile(setPathUtf8, pluginRootUtf8);
    result.warnings = resolved.warnings;
    if (!resolved.ok) return result;
    result.settingName = resolved.settingName;

    for (const std::string& folder : resolved.folders) {
        std::string err;
        if (const std::optional<std::string> registered = selector.EnsureTrackPluginLoaded(folder, err)) {
            result.resolvedFolders.push_back(*registered);
        } else {
            result.warnings.push_back("読込失敗: " + folder + " (" + err + ")");
        }
    }

    result.ok = true;
    return result;
}

} // namespace rs::app

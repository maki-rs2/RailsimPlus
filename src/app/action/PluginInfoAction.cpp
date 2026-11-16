#include "PluginInfoAction.h"

#include <algorithm>
#include <map>
#include <utility>

#include "../../plugins/schema/PluginSchema.h"
#include "../plugin_session/PluginSession.h"

namespace rs::app {

PluginInfoAction::PluginInfoAction(PluginSession& session) : session_(session) {}

void PluginInfoAction::SetSwitch(std::size_t index, std::int32_t value) {
    const std::vector<rs::legacy_pi::SwitchDef>& defs = session_.plugin.def.switches;
    if (index >= defs.size()) {
        return;
    }
    const std::map<std::string, std::int32_t>::iterator switchIt =
        session_.UserSwitches().find(defs[index].name);
    if (switchIt == session_.UserSwitches().end()) {
        return;
    }
    switchIt->second = value;
}

PluginInfoViewModel PluginInfoAction::ViewModel() const {
    PluginInfoViewModel viewModel;
    viewModel.loaded = session_.plugin.ok;
    if (!viewModel.loaded) {
        return viewModel;
    }

    viewModel.pluginName = session_.plugin.def.header.name;
    viewModel.typeName = rs::legacy_pi::PluginTypeName(session_.plugin.def.header.type);
    viewModel.author = session_.plugin.def.header.author;
    viewModel.partCount = session_.plugin.models.size();
    viewModel.missingFiles = session_.plugin.missingFiles;
    viewModel.switches = BuildSwitchRows(
        session_.plugin.def.switches,
        [this](std::size_t, const rs::legacy_pi::SwitchDef& def) -> std::int32_t {
            const std::map<std::string, std::int32_t>::const_iterator switchIt =
                session_.UserSwitches().find(def.name);
            return switchIt != session_.UserSwitches().end() ? switchIt->second : 0;
        });
    return viewModel;
}

} // namespace rs::app

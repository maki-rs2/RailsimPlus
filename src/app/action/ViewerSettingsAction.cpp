#include "ViewerSettingsAction.h"

#include <map>
#include <utility>

#include "../plugin_session/PluginSession.h"
#include "../plugin_session/ViewerState.h"

namespace rs::app {

ViewerSettingsAction::ViewerSettingsAction(PluginSession& session, ViewerState& viewer)
    : session_(session), viewer_(viewer) {}

void ViewerSettingsAction::SetLightOn(bool lightOn) {
    SetViewerLight(viewer_, session_, lightOn);
}

void ViewerSettingsAction::SetSystemSwitch(const std::string& name, std::int32_t value) {
    if (!rs::legacy_pi::IsSystemSwitchUserEditable(name)) {
        // 毎フレーム自動計算して上書きするため、手動編集させると値が戻り混乱する。
        return;
    }
    const std::map<std::string, std::int32_t>::iterator switchIt = session_.SystemSwitches().find(name);
    if (switchIt == session_.SystemSwitches().end()) {
        return;
    }
    switchIt->second = value;
}

ViewerSettingsViewModel ViewerSettingsAction::ViewModel() const {
    ViewerSettingsViewModel viewModel;
    viewModel.lightOn = viewer_.lightOn;
    for (const std::pair<const std::string, std::int32_t>& switchValue : session_.SystemSwitches()) {
        ViewerSystemSwitchRow row;
        row.name = switchValue.first;
        row.value = switchValue.second;
        // 表にない名前は編集できる数値欄として扱う。
        const rs::legacy_pi::SystemSwitchAxis* axis = rs::legacy_pi::FindSystemSwitchAxis(row.name);
        if (axis != nullptr) {
            row.readOnly = !axis->userEditable;
            row.widget = SystemSwitchWidgetFor(*axis);
            row.minValue = axis->domainMin;
            row.maxValue = axis->domainMax;
        }
        viewModel.systemSwitches.push_back(std::move(row));
    }
    return viewModel;
}

} // namespace rs::app

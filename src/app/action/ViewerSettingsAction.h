//
// ViewerSettingsAction.h - プラグインビューアーの「ビューアー設定」ウィンドウの操作と表示用の値
//
// 操作はここを通し、パネルは ViewModel を描くだけにする。
//
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../../plugins/schema/SystemSwitchAxes.h"

namespace rs::app {

struct PluginSession;
struct ViewerState;

// システムスイッチ1行の入力部品の種類。
enum class SystemSwitchWidget { Checkbox, Slider, Number };

// 表の行(domain・最小・最大)から入力部品を決める。スイッチ名では分岐しない。
// Binary はチェックボックス、範囲のある Enum(最小<最大)はスライダー、それ以外は数値欄。
inline SystemSwitchWidget SystemSwitchWidgetFor(const rs::legacy_pi::SystemSwitchAxis& axis) {
    switch (axis.domain) {
    case rs::legacy_pi::SystemSwitchDomain::Binary: return SystemSwitchWidget::Checkbox;
    case rs::legacy_pi::SystemSwitchDomain::Enum:
        return axis.domainMin < axis.domainMax ? SystemSwitchWidget::Slider : SystemSwitchWidget::Number;
    case rs::legacy_pi::SystemSwitchDomain::Continuous:
    case rs::legacy_pi::SystemSwitchDomain::Bitmask: break;
    }
    return SystemSwitchWidget::Number;
}

struct ViewerSystemSwitchRow {
    std::string name;
    std::int32_t value = 0;
    // 自動計算の値(表の userEditable が偽)。表示だけにして編集させない。
    bool readOnly = false;
    SystemSwitchWidget widget = SystemSwitchWidget::Number;
    // スライダーの範囲(widget が Slider のときだけ使う)。
    std::int32_t minValue = 0;
    std::int32_t maxValue = 0;
};

struct ViewerSettingsViewModel {
    bool lightOn = true;
    std::vector<ViewerSystemSwitchRow> systemSwitches;
};

class ViewerSettingsAction {
public:
    ViewerSettingsAction(PluginSession& session, ViewerState& viewer);

    void SetLightOn(bool lightOn);
    // 自動計算のスイッチ(表の userEditable が偽)と、プラグインが参照していない名前は無視する。
    void SetSystemSwitch(const std::string& name, std::int32_t value);
    ViewerSettingsViewModel ViewModel() const;

private:
    PluginSession& session_;
    ViewerState& viewer_;
};

} // namespace rs::app

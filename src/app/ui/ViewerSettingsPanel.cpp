//
// ViewerSettingsPanel.cpp - 「ビューアー設定」ウィンドウ描画(プラグインビューアーモード専用)
//
// 値は ViewerSettingsAction の ViewModel から読み、
// 変更は Action へ返す。
//
#include "UiPanels.h"

#include "imgui.h"

namespace rs::app {

namespace {

// ウィンドウの初期位置(「プラグイン」パネルの右隣)と大きさ。
constexpr float kWindowPosX = 440.0f;
constexpr float kWindowPosY = 10.0f;
constexpr float kWindowWidth = 320.0f;
constexpr float kWindowHeight = 400.0f;

void DrawLightSection(ViewerSettingsAction& action, const ViewerSettingsViewModel& viewModel) {
    ImGui::TextUnformatted("ライト");
    ImGui::SameLine();
    if (ImGui::RadioButton("ON(昼)", viewModel.lightOn)) action.SetLightOn(true);
    ImGui::SameLine();
    if (ImGui::RadioButton("OFF(夜)", !viewModel.lightOn)) action.SetLightOn(false);
}

// 1スイッチ分の入力部品。部品の種類は ViewModel(表の列から決めた値)に従い、名前では分岐しない。
void DrawSystemSwitchRow(ViewerSettingsAction& action, const ViewerSystemSwitchRow& row) {
    if (row.readOnly) {
        ImGui::Text("%s = %d (自動計算)", row.name.c_str(), static_cast<int>(row.value));
        return;
    }
    switch (row.widget) {
    case SystemSwitchWidget::Checkbox: {
        bool checked = row.value != 0;
        if (ImGui::Checkbox(row.name.c_str(), &checked)) action.SetSystemSwitch(row.name, checked ? 1 : 0);
        break;
    }
    case SystemSwitchWidget::Slider: {
        int value = static_cast<int>(row.value);
        if (ImGui::SliderInt(row.name.c_str(), &value, static_cast<int>(row.minValue),
                             static_cast<int>(row.maxValue))) {
            action.SetSystemSwitch(row.name, value);
        }
        break;
    }
    case SystemSwitchWidget::Number: {
        int value = static_cast<int>(row.value);
        if (ImGui::InputInt(row.name.c_str(), &value)) action.SetSystemSwitch(row.name, value);
        break;
    }
    }
}

void DrawSystemSwitchSection(ViewerSettingsAction& action, const ViewerSettingsViewModel& viewModel) {
    if (viewModel.systemSwitches.empty()) return;
    ImGui::Separator();
    ImGui::Text("システムスイッチ (%zu)", viewModel.systemSwitches.size());
    for (std::size_t rowIndex = 0; rowIndex < viewModel.systemSwitches.size(); ++rowIndex) {
        ImGui::PushID(static_cast<int>(rowIndex));
        DrawSystemSwitchRow(action, viewModel.systemSwitches[rowIndex]);
        ImGui::PopID();
    }
}

} // namespace

void DrawViewerSettingsPanel(const ViewerSettingsPanelContext& ctx) {
    ImGui::SetNextWindowPos(ImVec2(kWindowPosX, kWindowPosY), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(kWindowWidth, kWindowHeight), ImGuiCond_FirstUseEver);
    ImGui::Begin("ビューアー設定");
    const ViewerSettingsViewModel viewModel = ctx.action.ViewModel();
    DrawLightSection(ctx.action, viewModel);
    DrawSystemSwitchSection(ctx.action, viewModel);
    ImGui::End();
}

} // namespace rs::app

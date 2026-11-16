//
// RenderSettingsPanel.cpp - 「描画設定」パネル描画
//
// 行は RenderSettingsRows.h の表から作る(RmlUi版の描画タブと同じ表)。値の保存と反映は
// RenderSettingsActionが行う。
//
#include "UiPanels.h"

#include <string>

#include "imgui.h"

#include "../action/RenderSettingsRows.h"

namespace rs::app {
namespace {

// 行の1つを描く。値を替えたときだけ書き込む。
void DrawRow(const RenderSettingRow& row, const RenderSettingsViewModel& viewModel, RenderSettingsAction& action,
             const Localization& localization) {
    const std::string label = localization.Text(row.labelKey) + "##" + row.key;
    ImGui::BeginDisabled(IsRenderSettingRowDisabled(row, viewModel));
    switch (row.kind) {
    case RenderSettingKind::Toggle: {
        bool enabled = row.get(viewModel) != 0;
        if (ImGui::Checkbox(label.c_str(), &enabled)) row.set(action, enabled ? 1 : 0);
        break;
    }
    case RenderSettingKind::Choice: {
        const std::size_t current = RenderChoiceSelectedIndex(row, viewModel);
        if (ImGui::BeginCombo(label.c_str(), row.choices->label(current, localization).c_str())) {
            for (std::size_t i = 0; i < row.choices->count; ++i) {
                const std::string optionLabel = row.choices->label(i, localization);
                if (ImGui::Selectable(optionLabel.c_str(), i == current)) row.set(action, row.choices->value(i));
            }
            ImGui::EndCombo();
        }
        break;
    }
    case RenderSettingKind::LogSlider: {
        // 描画距離(遠クリップ面)。小さくすると遠方が切り落とされ、常駐スロットの
        // フラスタムカリング(視錐台は同じviewProjから抽出)も併せて適用される。
        float value = row.getFloat(viewModel);
        const std::string format = std::string("%.0f ") + row.unit;
        if (ImGui::SliderFloat(label.c_str(), &value, RenderSettingsAction::RenderDistanceMin(), RenderSettingsAction::RenderDistanceMax(), format.c_str(),
                               ImGuiSliderFlags_Logarithmic)) {
            row.setFloat(action, value);
        }
        break;
    }
    }
    ImGui::EndDisabled();
    if (ShouldShowRenderSettingNote(row, viewModel)) ImGui::TextDisabled("%s", localization.Text(row.noteKey).c_str());
}

} // namespace

void DrawRenderSettingsPanel(const RenderSettingsPanelContext& ctx) {
    ImGui::Begin("描画設定");

    const RenderSettingsViewModel viewModel = ctx.action.ViewModel();
    const char* previousSection = nullptr;
    for (const RenderSettingRow& row : RenderSettingRows()) {
        if (previousSection == nullptr || std::string(previousSection) != row.sectionKey) {
            ImGui::SeparatorText(ctx.localization.Text(row.sectionKey).c_str());
        }
        previousSection = row.sectionKey;
        DrawRow(row, viewModel, ctx.action, ctx.localization);
    }

    ImGui::End();
}

} // namespace rs::app

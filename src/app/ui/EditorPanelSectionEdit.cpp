//
// EditorPanelSectionEdit.cpp - edge内区間分割編集UI
//
#include "EditorPanelSectionEdit.h"

#include <cstdio>
#include <string>

#include "imgui.h"

#include "../../editor/tool/EditorSectionEdit.h"

namespace rs::app {
namespace {

std::string SectionValueLabel(const EdgeSectionRowViewModel& row) {
    if (row.pluginFolderCount == 0) return "(なし)";
    char buf[160];
    std::snprintf(buf, sizeof(buf), "%zu件: %s", row.pluginFolderCount,
                  row.firstPluginFolder.c_str());
    return buf;
}

} // namespace

void DrawEdgeSectionEditSection(const EdgeSectionEditContext& ctx) {
    const EdgeSectionEditViewModel viewModel = ctx.action.ViewModel();
    if (!viewModel.visible) return;

    ImGui::Separator();
    ImGui::Text("区間分割");

    bool splitMode = viewModel.splitMode;
    if (ImGui::Checkbox("分割モード(ビューポート)", &splitMode)) {
        ctx.action.SetSplitMode(splitMode);
    }
    if (viewModel.splitMode) {
        ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.4f, 1.0f),
                           "軌道上をクリックすると分割します");
    }

    if (!viewModel.hasSpline) return;

    ImGui::BeginChild("sectionlist", ImVec2(0, 120), true);
    for (const EdgeSectionRowViewModel& row : viewModel.rows) {
        const std::string valueLabel = SectionValueLabel(row);
        char label[192];
        std::snprintf(label, sizeof(label), "%2zu: %.1f–%.1f m  %s##section%zu", row.index,
                      row.sStart, row.sEnd, valueLabel.c_str(), row.index);
        if (ImGui::Selectable(label, row.selected)) ctx.action.SelectSection(row.index);
    }
    ImGui::EndChild();

    if (viewModel.hasSelectedBoundary) {
        float boundaryEdit = static_cast<float>(viewModel.selectedBoundaryS);
        if (ImGui::InputFloat("境界s[m]", &boundaryEdit, 0.0f, 0.0f, "%.2f",
                              ImGuiInputTextFlags_EnterReturnsTrue)) {
            ctx.action.MoveSelectedBoundary(static_cast<double>(boundaryEdit));
        }
        if (ImGui::Button("この境界を削除")) ctx.action.RemoveSelectedBoundary();
    }

    ImGui::TextDisabled("プラグインの追加/削除は「レール系プラグイン」パネルで行います");
}

} // namespace rs::app

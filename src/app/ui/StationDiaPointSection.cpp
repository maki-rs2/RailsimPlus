#include "StationDiaPointSection.h"

#include "imgui.h"

namespace rs::app {

void DrawStationDiaPointSection(StationDiaAction& action,
                                const std::vector<StationDiaPointRowViewModel>& points) {
    if (points.empty()) return;
    ImGui::Separator();
    if (!ImGui::CollapsingHeader("分岐(ポイント)", ImGuiTreeNodeFlags_DefaultOpen)) return;
    for (std::size_t rowIndex = 0; rowIndex < points.size(); ++rowIndex) {
        const StationDiaPointRowViewModel& point = points[rowIndex];
        // ImGuiのIDは行位置で固定する(Guidの文字列化は毎フレームの負荷が大きいため)。
        ImGui::PushID(static_cast<int>(rowIndex));
        if (ImGui::Selectable("##point", point.selected, 0, ImVec2(0.0f, 0.0f))) {
            action.SelectPoint(point.id);
        }
        if (ImGui::IsItemHovered()) action.SetHoveredPoint(point.id);
        ImGui::SameLine();
        ImGui::Text("ポイント%d (x %.1f, z %.1f)", static_cast<int>(rowIndex) + 1, point.x, point.z);
        for (std::size_t sideIndex = 0; sideIndex < point.sides.size(); ++sideIndex) {
            const StationDiaPointSideViewModel& side = point.sides[sideIndex];
            ImGui::PushID(static_cast<int>(sideIndex));
            ImGui::Indent();
            ImGui::TextUnformatted("開通:");
            ImGui::SameLine();
            if (ImGui::RadioButton("左", side.leftActive) && !side.leftActive) {
                action.SetPointActiveLink(point.id, side.leftLinkIndex);
            }
            if (ImGui::IsItemHovered()) action.SetHoveredPoint(point.id);
            ImGui::SameLine();
            if (ImGui::RadioButton("右", side.rightActive) && !side.rightActive) {
                action.SetPointActiveLink(point.id, side.rightLinkIndex);
            }
            if (ImGui::IsItemHovered()) action.SetHoveredPoint(point.id);
            ImGui::Unindent();
            ImGui::PopID();
        }
        ImGui::PopID();
    }
}

}  // namespace rs::app

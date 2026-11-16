#include "SwitchListWidget.h"

#include <cstdio>
#include <string>

#include "../action/SignalPluginRules.h"
#include "imgui.h"

namespace rs::app {

void DrawSwitchList(const char* childId, const std::vector<SwitchRowViewModel>& rows,
                    SwitchValueWriter& writer) {
    ImGui::BeginChild(childId, ImVec2(0, 120), true);
    for (std::size_t index = 0; index < rows.size(); ++index) {
        const SwitchRowViewModel& row = rows[index];
        const SwitchRowKind kind = GetSwitchRowKind(row);
        if (kind == SwitchRowKind::Header) {
            ImGui::SeparatorText(row.name.c_str());
            continue;
        }
        if (kind == SwitchRowKind::Spacer) {
            ImGui::Spacing();
            continue;
        }
        if (row.linkedToBlock) {
            ImGui::TextDisabled("%s", row.name.c_str());
            ImGui::SameLine();
            ImGui::TextUnformatted(kSignalSwitchLinkedToBlockText);
            continue;
        }
        // 同名スイッチでもImGuiのIDが衝突しないよう"##swN"を付ける。
        char label[512];
        std::snprintf(label, sizeof(label), "%s##sw%d", row.name.c_str(), static_cast<int>(index));
        ImGui::PushID(static_cast<int>(index));
        int value = row.value;
        std::vector<const char*> items;
        items.reserve(row.entries.size());
        for (const std::string& entry : row.entries) {
            items.push_back(entry.c_str());
        }
        if (ImGui::Combo(label, &value, items.data(), static_cast<int>(items.size()))) {
            writer.SetSwitch(index, value);
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
}

} // namespace rs::app

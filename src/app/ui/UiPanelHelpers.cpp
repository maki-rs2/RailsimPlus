//
// UiPanelHelpers.cpp
//
#include "UiPanelHelpers.h"

#include <cstdio>
#include <optional>

#include "imgui.h"

#include "../plugin_session/PluginSelector.h"

namespace rs::app {

void DrawAddRow(PluginSelector& selector, ImGuiPathPicker& pathPicker, std::string& lastError,
                const char* addButtonId) {
    char addLbl[48];
    std::snprintf(addLbl, sizeof(addLbl), "フォルダを追加…##%s", addButtonId);
    if (const std::optional<std::string> picked = pathPicker.Button(addLbl, FolderPick("プラグインのフォルダを選択"))) {
        std::string err;
        if (selector.AddPath(*picked, err)) {
            lastError.clear();
        } else {
            lastError = err;
        }
    }
    if (!lastError.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", lastError.c_str());
    }
    ImGui::Separator();
}

} // namespace rs::app

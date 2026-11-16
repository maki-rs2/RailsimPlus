//
// SceneEntitySwitchPanel.cpp - 「配置スイッチ」セクション
//
#include "UiPanels.h"

#include "SwitchListWidget.h"
#include "../../world/alignment/GuidJson.h"
#include "imgui.h"

namespace rs::app {

void DrawSceneEntitySwitchSection(const SceneEntitySwitchContext& ctx) {
    const SceneEntitySwitchViewModel viewModel = ctx.action.ViewModel();
    if (!viewModel.visible) {
        return;
    }

    ImGui::Separator();
    ImGui::TextUnformatted("配置スイッチ (選択中)");
    ImGui::TextWrapped("%s", viewModel.folderUtf8.c_str());
    ImGui::Text("id: %s", rs::world::GuidToHex(viewModel.entityId).c_str());
    ImGui::Separator();

    if (viewModel.rows.empty()) {
        ImGui::TextUnformatted("このプラグインにスイッチはありません");
        return;
    }

    DrawSwitchList("scene_entity_switches", viewModel.rows, ctx.action);
}

bool DrawSceneEntitySwitchWindow(const SceneEntitySwitchContext& ctx) {
    const SceneEntitySwitchViewModel viewModel = ctx.action.ViewModel();
    if (!viewModel.visible) return false;

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(420, 300), ImGuiCond_FirstUseEver);
    bool open = true;
    // ##を付けて、選択中の配置スイッチを出す「プラグイン」パネルとは別のウィンドウIDにする。
    if (ImGui::Begin("プラグイン設定##viewport_click", &open)) {
        ImGui::TextWrapped("%s", viewModel.folderUtf8.c_str());
        ImGui::Text("id: %s", rs::world::GuidToHex(viewModel.entityId).c_str());
        ImGui::Separator();
        if (viewModel.rows.empty()) {
            ImGui::TextUnformatted("このプラグインにスイッチはありません");
        } else {
            DrawSwitchList("scene_entity_switches_window", viewModel.rows, ctx.action);
        }
    }
    ImGui::End();
    return !open;
}

} // namespace rs::app

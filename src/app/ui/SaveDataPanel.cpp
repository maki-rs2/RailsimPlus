#include "UiPanels.h"

#include <optional>

#include <magic_enum/magic_enum.hpp>

#include "imgui.h"

#include "../plugin_session/PluginSelector.h"
#include "../../platform/file/ShellFileTypes.h"

namespace rs::app {

std::string LayoutActionErrorMessage(const LayoutActionResult& result, const Localization& localization) {
    std::string message = localization.Text(result.messageKey);
    if (!result.detail.empty()) message += ": " + result.detail;
    // 機械可読の識別子は重大な失敗のときだけ併記する(不具合報告で必要になるため)。
    // ユーザーが直して再試行できる失敗では文言だけを出す。
    if (SeverityOf(result.status) != ErrorSeverity::Critical) return message;
    return message + " (" + std::string(magic_enum::enum_name(result.status)) + ")";
}

void DrawSaveDataPanel(const SaveDataPanelContext& ctx) {
    ImGui::Begin("セーブデータ");
    ImGui::TextUnformatted("レイアウトの読み込み(.rs2 / .rsl)");
    if (const std::optional<std::string> picked =
            ctx.pathPicker.Button("ファイルを選んで開く…##savedata_layout_load",
                                  FilePick("レイアウトを開く", rs::platform_file::kLayoutFileType))) {
        // 実行は次フレーム以降(TickPendingLayoutLoad)。起動時CLI(--openlayout)と
        // 同じ予約経路に乗せる。結果反映(ログ追記・
        // lastError更新)もそちらに一本化しているため、ここでは行わない。
        ctx.action.RequestLoadLayout(*picked);
    }
    const LayoutSceneViewModel sceneViewModel = ctx.action.SceneViewModel();
    if (sceneViewModel.hasMultipleScenes) {
        ImGui::Separator();
        ImGui::Text("Scene %zu/%zu: %s", sceneViewModel.activeSceneIndex + 1,
                    sceneViewModel.sceneCount, sceneViewModel.activeSceneName.c_str());
        if (ImGui::Button("< 前のScene##layout_scene_prev")) {
            ctx.action.SwitchScene(-1);
        }
        ImGui::SameLine();
        if (ImGui::Button("次のScene >##layout_scene_next")) {
            ctx.action.SwitchScene(+1);
        }
    }
    ImGui::Separator();
    const std::map<rs::legacy_pi::PluginType, std::string>& slots = ctx.selector.SingleSlots();
    const std::map<rs::legacy_pi::PluginType, std::string>::const_iterator surface =
        slots.find(rs::legacy_pi::PluginType::Surface);
    const bool hasSurface = surface != slots.end() && !surface->second.empty();
    ImGui::BeginDisabled(!hasSurface);
    if (ImGui::Button("シーンを追加##layout_scene_add")) {
        const LayoutActionResult result = ctx.action.AddScene(surface->second);
        if (result.Ok()) {
            ctx.state.lastError.clear();
            ctx.log.push_back("[scene] 追加: " + ctx.action.SceneViewModel().activeSceneName);
        } else {
            ctx.state.lastError = LayoutActionErrorMessage(result, ctx.localization);
        }
    }
    ImGui::EndDisabled();
    if (!hasSurface) {
        ImGui::SameLine();
        ImGui::TextDisabled("Surfaceを選んでください");
    }
    ImGui::Separator();
    ImGui::TextUnformatted("レイアウトの保存(ファイル名。拡張子は自動で .rsl)");
    ImGui::InputText("ファイル名##savedata_rsl_save", ctx.state.rslSaveNameBuf,
                     sizeof(ctx.state.rslSaveNameBuf));
    ImGui::SameLine();
    if (ImGui::Button("保存##savedata_rsl_save")) {
        const LayoutActionResult result = ctx.action.SaveRsl(ctx.state.rslSaveNameBuf);
        if (result.Ok()) {
            ctx.state.lastError.clear();
            ctx.log.push_back("[rsl] 保存: " + result.savedPathUtf8);
        } else {
            ctx.state.lastError = LayoutActionErrorMessage(result, ctx.localization);
        }
    }
    if (!ctx.state.lastError.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", ctx.state.lastError.c_str());
    }
    ImGui::End();
}

}  // namespace rs::app

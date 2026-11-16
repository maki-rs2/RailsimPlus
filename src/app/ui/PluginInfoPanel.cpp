//
// PluginInfoPanel.cpp - 「プラグイン」パネル描画
//
// プラグイン選択系パネル(レール系/編成/情景)の「表示」ボタン経由で
// 読み込まれた PluginSession の内容(PI情報/スイッチ/ログ)のみを表示する。
//
#include "UiPanels.h"

#include <vector>

#include "SwitchListWidget.h"
#include "imgui.h"

namespace rs::app {

namespace {

// PI情報(プラグイン名/type/作者/パーツ数)。
void DrawPluginHeaderInfo(const PluginInfoViewModel& viewModel) {
    ImGui::Text("Plugin: %s (%s) by %s", viewModel.pluginName.c_str(), viewModel.typeName.c_str(),
                viewModel.author.c_str());
    ImGui::Text("Parts: %zu  missingFiles: %d", viewModel.partCount, viewModel.missingFiles);
}

// カスタムスイッチ(DefineSwitch)のUI。システムスイッチはビューアー設定ウィンドウに出す。
void DrawSwitchUi(PluginInfoAction& action, const PluginInfoViewModel& viewModel) {
    if (!viewModel.switches.empty()) {
        ImGui::Separator();
        ImGui::Text("スイッチ (%zu)", viewModel.switches.size());
        DrawSwitchList("switches", viewModel.switches, action);
    }
}

} // namespace

void DrawPluginInfoPanel(const PluginInfoPanelContext& ctx) {
    ImGui::Begin("プラグイン");
    const PluginInfoViewModel viewModel = ctx.action.ViewModel();
    if (viewModel.loaded) {
        DrawPluginHeaderInfo(viewModel);
        DrawSwitchUi(ctx.action, viewModel);
    } else {
        ImGui::TextUnformatted("表示中のプラグインはありません。");
    }

    // レイアウト上の配置を選択しているときは、その配置に焼き込まれているスイッチ値も
    // 同じパネルに出す(上のスイッチUIは単体表示中プラグインのもので、対象が異なる)。
    DrawSceneEntitySwitchSection({ctx.sceneEntitySwitchAction});

    ImGui::Separator();
    ImGui::Text("読込ログ (%zu行):", ctx.loadLog.size());
    ImGui::BeginChild("log", ImVec2(0, 150), true);
    // 大規模プラグイン(テクスチャが無い警告等で数万行に達することがある)でも
    // 毎フレームのコストを表示行数のみに抑えるため、ImGuiListClipperで
    // 可視範囲だけ描画する(全件TextUnformattedはフレーム時間の
    // 大半を占める)。
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(ctx.loadLog.size()));
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            ImGui::TextUnformatted(ctx.loadLog[static_cast<size_t>(i)].c_str());
        }
    }
    clipper.End();
    ImGui::EndChild();

    ImGui::End();
}

} // namespace rs::app

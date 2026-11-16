//
// PluginSelectorPanel.cpp - プラグイン選択系パネル(レール系/編成/情景)描画
// (main.cppから分離)
//
#include "UiPanels.h"
#include "EditorViewportState.h"

#include "../../plugins/schema/PluginTypeTraits.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

#include "imgui.h"

#include "../../platform/file/ShellFileTypes.h"
#include "../../plugins/schema/PluginSchema.h"
#include "../../world/scene/ScenePlacement.h"
#include "../../editor/tool/Editor.h"
#include "../plugin_session/PluginSelector.h"
#include "../action/SceneryPlacementAction.h"
#include "../action/SceneryYawAction.h"
#include "../action/TrackPluginSetAction.h"
#include "../action/TrackPluginSetText.h"
#include "../../world/alignment/GuidJson.h"
#include "../../world/scene/SceneEntity.h"
#include "../../world/scene/SceneRegistry.h"
#include "UiPanelHelpers.h"

namespace rs::app {

void DrawMaterializedTrackPluginSection(rs::editor::Editor& editor, PluginSelector& selector,
                                        EditorViewportState& state);

void DrawTrackPluginListPanel(rs::editor::Editor& editor, PluginSelector& selector,
                             ImGuiPathPicker& pathPicker, std::string& lastError,
                             std::string& templateLastError,
                             const rs::world::SceneRegistry& sceneRegistry, EditorViewportState& state) {
    ImGui::Begin("レール系プラグイン");

    // edge選択時は「区間分割」パネル(EditorPanelSectionEdit)で選択中のPluginSet層区間の
    // pluginSections、未選択時は敷設テンプレート(defaultTrackPlugins)が編集対象。
    // 対象の切り分けと書き込みはTrackPluginSetActionが受け持つ。
    TrackPluginSetAction action(editor, selector);
    const TrackPluginSetViewModel viewModel = action.ViewModel();
    const bool edgeValid = viewModel.target == TrackPluginSetTarget::Section;

    if (edgeValid && viewModel.selectedEdgeCount > 1) {
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.6f, 1.0f),
                           "選択中の%zu本の共通のプラグイン (%zu件)", viewModel.selectedEdgeCount,
                           viewModel.items.size());
    } else if (edgeValid) {
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.6f, 1.0f),
                           "選択edgeのプラグイン一覧 (%zu件) 区間%zu/%zu", viewModel.items.size(),
                           viewModel.sectionIndex + 1, viewModel.sectionCount);
    } else
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f),
                           "次に敷設するプラグイン一覧 (%zu件、edgeを選択するとそのedgeを編集)",
                           viewModel.items.size());
    ImGui::TextDisabled("Rail/Tie/Girderは連続押し出し。Pierのみ離散配置(間隔はPI定義値、手動調整なし)");

    if (const std::optional<std::string> picked =
            pathPicker.Button("フォルダを追加…##rail", FolderPick("プラグインのフォルダを選択"))) {
        lastError = TrackPluginSetErrorText(action.Add(*picked));
    }
    if (!lastError.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", lastError.c_str());
    ImGui::Separator();

    // 敷設テンプレート(RailwayPluginSet\*.txt)一括読込。重複排除と区間への書き戻しは
    // TrackPluginSetAction::ApplyTemplateFileが行う。
    ImGui::TextUnformatted("敷設テンプレート読込(RailwayPluginSet)");
    if (const std::optional<std::string> picked = pathPicker.Button(
            "ファイルを選んで読込…##railwaypluginset",
            FilePick("敷設テンプレートを選択", rs::platform_file::kTextFileType))) {
        templateLastError = TrackPluginSetErrorText(action.ApplyTemplateFile(*picked));
    }
    if (!templateLastError.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", templateLastError.c_str());
    ImGui::Separator();

    // 上限はSRVプール(kSceneSrvPoolCapacity)の残量に依存する可変値のため固定数との
    // 比較表示はしない。枯渇時はSceneRenderer::Sync()がlogへ警告を出す。
    const size_t sceneCount = static_cast<size_t>(std::count_if(
        sceneRegistry.Entities().begin(), sceneRegistry.Entities().end(),
        [](const rs::world::SceneEntity& entity) { return rs::world::IsUserPlacement(entity); }));
    ImGui::Text("シーン常駐: %zu", sceneCount);
    ImGui::TextDisabled(edgeValid ? "追加/削除はUndo対応" : "追加/削除はUndo対象外(テンプレートはグラフに属さない)");

    // 削除はループ中に対象を書き換えないよう、保留してループを抜けてから反映する。
    std::string pendingRemove;
    ImGui::BeginChild("railplugin", ImVec2(0, 0), true);
    for (const TrackPluginSetItem& item : viewModel.items) {
        ImGui::PushID(item.folderUtf8.c_str());
        if (item.loaded) {
            ImGui::Text("[%s] %s", rs::legacy_pi::PluginTypeName(item.type), item.displayName.c_str());
            if (item.discrete) {
                ImGui::SameLine();
                ImGui::TextDisabled("間隔=%.1fm(PI定義)", item.intervalMeters);
            }
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.4f, 1.0f), "(未読込) %s", item.folderUtf8.c_str());
        }
        ImGui::SameLine();
        if (ImGui::Button("削除")) pendingRemove = item.folderUtf8;
        // edge内での有効範囲(区間ごとのfolders所属)は「区間分割」パネル
        // (EditorPanelSectionEdit)で編集する。
        ImGui::PopID();
        if (!pendingRemove.empty()) break;
    }
    ImGui::EndChild();
    if (!pendingRemove.empty()) action.Remove(pendingRemove);
    if (edgeValid) DrawMaterializedTrackPluginSection(editor, selector, state);
    ImGui::End();
}

void DrawMaterializedTrackPluginSection(rs::editor::Editor& editor, PluginSelector& selector,
                                        EditorViewportState& state) {
    // 確認ダイアログ表示中の解除対象。edgeも覚えるのは、ダイアログを開いたまま別のedgeを
    // 選ばれても対象がすり替わらないようにするため。
    rs::world::Guid& pendingEdge = state.pendingEdge;
    std::string& pendingDematerialize = state.pendingDematerialize;
    std::vector<const rs::world::RunAnchor*> runAnchors;
    for (const rs::world::RunAnchor& section : editor.manualOverlay.runAnchors) {
        if (section.edge == editor.selectedEdge) runAnchors.push_back(&section);
    }
    if (runAnchors.empty()) return;

    ImGui::Separator();
    ImGui::TextUnformatted("実体化済み(個体を直接編集中。自動生成は停止しています)");
    for (const rs::world::RunAnchor* section : runAnchors) {
        ImGui::PushID(section->folderUtf8.c_str());
        const TrackPluginEntry* found = selector.FindTrackPluginEntry(section->folderUtf8);
        if (found != nullptr) {
            ImGui::Text("[%s] %s", rs::legacy_pi::PluginTypeName(found->type),
                        found->displayName.c_str());
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.4f, 1.0f), "(未読込) %s",
                               section->folderUtf8.c_str());
        }
        std::size_t individualCount = 0;
        for (const rs::world::PlacedIndividual& pole : editor.manualOverlay.poles) {
            if (pole.edge == section->edge && pole.folderUtf8 == section->folderUtf8)
                ++individualCount;
        }
        for (const rs::world::PlacedIndividual& pier : editor.manualOverlay.piers) {
            if (pier.edge == section->edge && pier.folderUtf8 == section->folderUtf8)
                ++individualCount;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("個体数: %zu", individualCount);
        ImGui::SameLine();
        bool requested = false;
        if (ImGui::Button("解除")) requested = true;
        ImGui::PopID();
        // OpenPopupとBeginPopupModalは同じIDスタックの下で呼ぶ必要がある。PushID中に
        // 開くとIDが食い違ってダイアログが出ないため、PopIDしてから開く。
        if (requested) {
            pendingEdge = section->edge;
            pendingDematerialize = section->folderUtf8;
            ImGui::OpenPopup("実体化を解除しますか");
        }
    }
    if (ImGui::BeginPopupModal("実体化を解除しますか", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("解除すると個別に編集した内容(移動/削除/架線の増減)は失われ、");
        ImGui::TextUnformatted("全区間で自動生成へ戻ります。");
        if (ImGui::Button("解除する")) {
            if (!pendingDematerialize.empty() && pendingEdge == editor.selectedEdge)
                editor.DematerializeSelectedEdgeFolder(pendingDematerialize);
            pendingDematerialize.clear();
            pendingEdge = rs::world::NilGuid();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("キャンセル")) {
            pendingDematerialize.clear();
            pendingEdge = rs::world::NilGuid();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void DrawSingleSlotListPanel(rs::editor::Editor& editor, PluginSelector& selector,
                             ImGuiPathPicker& pathPicker, std::string& lastError,
                             rs::world::SceneRegistry& sceneRegistry, SceneryPlacementUiState& uiState) {
    ImGui::Begin("情景プラグイン");
    SceneryPlacementAction placement(editor);
    DrawAddRow(selector, pathPicker, lastError, "slot");
    ImGui::Text("種別ごと単一選択(Station/Struct/Surface等)");
    // 配置座標入力。全エントリ共通で1組だけ持ち、
    // 「配置」ボタン押下時にFreePlacementへ変換する。値は次回配置時のデフォルトとして残る。
    ImGui::Text("配置座標");
    ImGui::DragFloat3("位置(X/Y/Z)", &uiState.position.x, 0.1f);
    // yaw/pitch/rollはSceneryPlacementUiState内で連続したfloatのためDragFloat3で1行にまとめる
    // (位置(X/Y/Z)と同じ「連続float3本をDragFloat3へ渡す」書き方、UiPanels.hのフィールド順序を
    // 変更する場合はここも合わせて見直すこと)。
    ImGui::DragFloat3("向き(yaw/pitch/roll)", &uiState.yawDeg, 1.0f);
    // 配置プレビュー中は左右矢印キーでyawが刻み角度ずつ回る(SceneryYawAction)。
    ImGui::InputFloat("矢印キーの回転刻み(度)", &uiState.yawStepDeg, 1.0f, 15.0f, "%.1f");
    uiState.yawStepDeg = SceneryYawAction::ClampStepDeg(uiState.yawStepDeg);
    ImGui::Checkbox("連続配置", &uiState.continuousPlacement);
    ImGui::Checkbox("レールに合わせる", &uiState.snapToRail);
    if (placement.Active()) {
        ImGui::SameLine();
        if (ImGui::Button("配置終了")) placement.End();
    }
    ImGui::BeginChild("singleslots", ImVec2(0, 0), true);
    for (const std::pair<const rs::legacy_pi::PluginType, std::string>& kv :
         selector.SingleSlots()) {
        ImGui::PushID(static_cast<int>(kv.first));
        ImGui::Text("[%s] %s", rs::legacy_pi::PluginTypeName(kv.first), kv.second.c_str());
        ImGui::SameLine();
        if (ImGui::Button("配置"))
            placement.Begin(kv.second, {uiState.continuousPlacement, uiState.stationAnchor});
        if (kv.first == rs::legacy_pi::PluginType::Station && SceneryPlacementAction::CanAnchor(kv.second)) {
            ImGui::SameLine();
            ImGui::Checkbox("アンカー設置", &uiState.stationAnchor);
        }
        ImGui::SameLine();
        bool removed = false;
        if (ImGui::Button("削除")) {
            selector.RemoveSingleSlot(kv.first);
            removed = true;
        }
        ImGui::PopID();
        if (removed) break;
    }
    ImGui::EndChild();

    // 配置済みStruct/Stationの絞り込み用: SingleSlots()は種別ごとに現在選択中の
    // フォルダを1件保持している(このパネルの「種別ごと単一選択」設計)。SceneEntity
    // 自体はPluginTypeを持たないため、folderUtf8がここのStruct/Stationいずれかの
    // フォルダと一致するものだけを削除一覧に出す(選択を切り替えて外れた別folderの
    // Struct/Station配置は、選択が現在その種別に残っていない限りここには出ない。
    // 「種別ごと単一選択」というUIの制約に合わせている)。
    std::vector<std::string> structOrStationFolders;
    for (const std::pair<const rs::legacy_pi::PluginType, std::string>& kv : selector.SingleSlots()) {
        if (rs::legacy_pi::GetPluginTypeTraits(kv.first).scenery) {
            structOrStationFolders.push_back(kv.second);
        }
    }

    ImGui::Separator();
    ImGui::TextUnformatted("配置済みStruct/Station");
    rs::world::Guid pendingEntityRemove = rs::world::NilGuid();
    ImGui::BeginChild("placedscenery", ImVec2(0, 120), true);
    for (const rs::world::SceneEntity& entity : sceneRegistry.Entities()) {
        if (entity.category != rs::world::SceneEntityCategory::Scenery || !rs::world::IsUserPlacement(entity)) {
            continue;
        }
        const bool isStructOrStation =
            std::find(structOrStationFolders.begin(), structOrStationFolders.end(),
                      entity.folderUtf8) != structOrStationFolders.end();
        if (!isStructOrStation) {
            continue;
        }
        ImGui::PushID(rs::world::GuidToHex(entity.guid).c_str());
        ImGui::Text("%s", entity.folderUtf8.c_str());
        ImGui::SameLine();
        if (ImGui::Button("削除")) {
            pendingEntityRemove = entity.guid;
        }
        ImGui::PopID();
        if (!rs::world::GuidIsNil(pendingEntityRemove)) {
            break;
        }
    }
    ImGui::EndChild();
    if (!rs::world::GuidIsNil(pendingEntityRemove)) {
        editor.SelectSceneEntity(pendingEntityRemove);
        editor.DeleteSelectedSceneEntities(sceneRegistry);
    }
    ImGui::End();
}

} // namespace rs::app

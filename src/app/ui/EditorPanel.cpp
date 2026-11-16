//
// EditorPanel.cpp - 敷設エディタパネル(DrawEditorPanel)の実装
//
#include "UiPanels.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "imgui.h"

#include "../action/TrackLayingPreviewColor.h"
#include "../action/TrackLayingText.h"

#include "EditorPanelEdgeSection.h"
#include "EditorPanelNodeSection.h"
#include "EditorPanelSectionEdit.h"

namespace rs::app {
namespace {

// 桁数(TrackLayingText.hのk*Decimals)からImGui::InputFloatの書式("%.Nf")を作る。
std::string DecimalsFormat(int decimals) {
    return "%." + std::to_string(decimals) + "f";
}

// 文の行をそのまま描く。色はTextSeverityから引く(文そのものはTrackLayingText.hが作る)。
void DrawTextLines(const std::vector<TextLine>& lines) {
    for (const TextLine& line : lines) {
        if (line.severity == TextSeverity::Warning) {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "%s", line.text.c_str());
        } else if (line.severity == TextSeverity::Error) {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", line.text.c_str());
        } else {
            ImGui::TextUnformatted(line.text.c_str());
        }
    }
}

// 敷設設定(ツール=敷設のときだけ出す)。
void DrawLayingSettings(const EditorPanelContext& ctx, const TrackLayingViewModel& viewModel) {
    ImGui::Separator();
    // プレビュー線の色分け凡例(3Dのプレビュー線と同じ色。出どころはPreviewColor)。
    ImGui::TextUnformatted("プレビュー:");
    for (const PreviewLegendEntry& entry : kPreviewLegend) {
        ImGui::SameLine();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(PreviewColor(entry.kind)), "%s", entry.label);
    }

    ImGui::TextDisabled("edgeに近づいてクリック: 途中から分岐 / Altを押しながらクリック: edge分割");
    ImGui::TextDisabled("Spaceを押している間: スナップ解除(ノード・並走・edge上)");
    bool angleSnapEnabled = viewModel.angleSnapEnabled;
    if (ImGui::Checkbox("角度スナップ(常時)", &angleSnapEnabled)) {
        ctx.action.SetAngleSnapEnabled(angleSnapEnabled);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("OFFでも、Shiftを押している間だけ角度を丸めます");
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    float angleSnapDegrees = viewModel.angleSnapDegrees;
    if (ImGui::InputFloat("刻み角[deg]", &angleSnapDegrees, 1.0f, 5.0f,
                          DecimalsFormat(kAngleSnapDecimals).c_str())) {
        ctx.action.SetAngleSnapDegrees(angleSnapDegrees);
    }

    ImGui::SetNextItemWidth(80);
    int trackCount = viewModel.trackCount;
    if (ImGui::InputInt("敷設本数", &trackCount)) {
        ctx.action.SetTrackCount(trackCount);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    float trackSpacing = viewModel.trackSpacing;
    if (ImGui::InputFloat("線路間隔[m]", &trackSpacing, 0.1f, 1.0f,
                          DecimalsFormat(kTrackSpacingDecimals).c_str())) {
        ctx.action.SetTrackSpacing(trackSpacing);
    }

    // 緩和曲線: 種別(なし/クロソイド/3次放物線/サイン半波長) + 片側長。
    // コンボの添字と(緩和の有無, 形状)の対応はTrackLayingText.hのkTransitionChoicesが持つ。
    int transitionComboIndex = viewModel.transitionComboIndex;
    ImGui::SetNextItemWidth(140);
    if (ImGui::Combo("緩和曲線", &transitionComboIndex, kTransitionLabels.data(), kTransitionChoiceCount)) {
        const TransitionChoice& choice = TransitionChoiceAt(transitionComboIndex);
        ctx.action.SetTransition(choice.enabled, choice.shape);
    }
    if (viewModel.transitionEnabled) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80);
        float transitionLength = viewModel.transitionLength;
        if (ImGui::InputFloat("長さ[m]", &transitionLength, 1.0f, 10.0f,
                              DecimalsFormat(kTransitionLengthDecimals).c_str())) {
            ctx.action.SetTransitionLength(transitionLength);
        }
        DrawTextLines(TransitionNoticeLines(viewModel));
    }

    ImGui::TextUnformatted(FormatPlaceHeight(viewModel).c_str());

    DrawTextLines(LayingStatusLines(viewModel));
}

// 配置物の手動追加と架線の手動接続/切断(ツール=選択のときだけ出す)。
// 実際の編集はビューポートのクリックで起きる(EditorViewport.cppが処理)。
void DrawSelectToolSections(const EditorPanelContext& ctx, const TrackLayingViewModel& viewModel) {
    ImGui::Separator();
    ImGui::Text("配置物 手動追加");
    if (std::optional<std::string> picked =
            ctx.pathPicker.Field("フォルダ##placementAdd", viewModel.placementAddFolder,
                                 FolderPick("配置するプラグインのフォルダを選択"))) {
        ctx.action.SetPlacementAddFolder(std::move(*picked));
    }
    bool placementAddMode = viewModel.placementAddMode;
    if (ImGui::Checkbox("追加モード(Viewportクリックで配置)", &placementAddMode)) {
        ctx.action.SetPlacementAddMode(placementAddMode);
    }
    if (viewModel.placementAddMode) {
        if (viewModel.placementAddFolder.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "フォルダを選んでください");
        } else {
            ImGui::TextColored(ImVec4(0.31f, 1.0f, 0.47f, 1.0f),
                               "edgeをクリックすると \"%s\" を1本追加します",
                               viewModel.placementAddFolder.c_str());
        }
    }

    ImGui::Separator();
    ImGui::Text("架線 手動接続/切断");
    bool wireEditMode = viewModel.wireEditMode;
    if (ImGui::Checkbox("配線編集モード(接続点を2つ選んで接続)", &wireEditMode)) {
        ctx.action.SetWireEditMode(wireEditMode);
    }
    if (viewModel.wireEditMode) {
        ImGui::TextColored(ImVec4(0.31f, 1.0f, 0.47f, 1.0f),
                           "接続点を2つ順にクリックして結びます。径間をクリックで選び、Deleteで切断します");
    }
}

} // namespace

void DrawEditorPanel(const EditorPanelContext& ctx) {
    TrackLayingViewModel viewModel = ctx.action.ViewModel();
    ImGui::Begin("敷設エディタ");
    {
        bool toolChanged = false;
        if (ImGui::RadioButton("選択", !viewModel.placeMode)) {
            ctx.action.SelectTool(false);
            toolChanged = true;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("敷設", viewModel.placeMode)) {
            ctx.action.SelectTool(true);
            toolChanged = true;
        }
        if (toolChanged) viewModel = ctx.action.ViewModel();

        ImGui::BeginDisabled(!viewModel.canUndo);
        if (ImGui::Button("Undo")) ctx.action.Undo();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!viewModel.canRedo);
        if (ImGui::Button("Redo")) ctx.action.Redo();
        ImGui::EndDisabled();

        ImGui::Text("nodes=%zu edges=%zu", viewModel.nodeCount, viewModel.edgeCount);

        if (viewModel.placeMode) DrawLayingSettings(ctx, viewModel);
        DrawTextLines(ArcPreviewLines(viewModel.preview));
        if (!viewModel.placeMode) DrawSelectToolSections(ctx, viewModel);

        // edge内区間分割編集UI(EditorPanelSectionEdit.cppへ分離)。
        DrawEdgeSectionEditSection({ctx.sectionEditAction});

        // 選択edge プロパティ編集UI(EditorPanelEdgeSection.cppへ分離)。
        DrawSelectedEdgeSection({ctx.edgeAction});

        // 選択node プロパティ編集UI(EditorPanelNodeSection.cppへ分離)。
        DrawSelectedNodeSection({ctx.nodeAction});
    }
    ImGui::End();
}

} // namespace rs::app

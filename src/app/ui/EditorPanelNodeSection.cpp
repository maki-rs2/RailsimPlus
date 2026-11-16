//
// EditorPanelNodeSection.cpp - 敷設エディタパネルの「選択node プロパティ」UI
//
#include "EditorPanelNodeSection.h"

#include <cstdio>

#include "imgui.h"

#include "../../world/alignment/GuidJson.h"

namespace rs::app {
namespace {

const char* EdgeMergeRefusalReasonJa(rs::world::EdgeMergeRefusal refusal) {
    switch (refusal) {
    case rs::world::EdgeMergeRefusal::None: return "";
    case rs::world::EdgeMergeRefusal::NotDegree2: return "選択nodeが2本のedgeの結節点ではありません";
    case rs::world::EdgeMergeRefusal::IsBranchingNode: return "選択nodeは分岐(ポイント)です";
    case rs::world::EdgeMergeRefusal::NoCurve: return "一方または両方のedgeに線形データがありません";
    case rs::world::EdgeMergeRefusal::ShapeUnsolvable: return "結合後の形を両端の姿勢から作れません";
    case rs::world::EdgeMergeRefusal::CurvatureDiscontinuous: return "結合点で曲率が不連続になります";
    // CheckEdgeMerge単体がこの理由を返すことはない
    // (Editor::MergeEdgeAtがEditorCenterlineMerge経由でグループ全体の
    // 可否を判定する)。UI側switch文の網羅性のため文言を置く。
    case rs::world::EdgeMergeRefusal::MultiTrackInvolved: return "複線中心線edge、または中心線に属するedgeです";
    case rs::world::EdgeMergeRefusal::LayerMismatch: return "edge同士の編集レイヤが異なります";
    case rs::world::EdgeMergeRefusal::CantDiscontinuous: return "結合後のカントが不連続になります";
    case rs::world::EdgeMergeRefusal::WarpOnSharedNode: return "選択nodeにワープ端点が設定されています";
    case rs::world::EdgeMergeRefusal::FormsLoop: return "結合すると1本のedgeが環になります(2本の反対側の端点が同じnodeです)";
    case rs::world::EdgeMergeRefusal::HasSeam: return "駅の番線につながる継ぎ目を持つedgeは結合できません";
    }
    return "不明な理由";
}

} // namespace

void DrawSelectedNodeSection(const NodeSectionContext& ctx) {
    const NodePropertyViewModel viewModel = ctx.action.ViewModel();
    if (!viewModel.visible) {
        return;
    }

    ImGui::Separator();
    ImGui::Text("選択node プロパティ");
    ImGui::Text("標高 %.2f m", viewModel.elevation);
    ImGui::TextDisabled("↑↓キー / Ctrl+ホイールで変更");
    ImGui::Text("接続edge数=%zu", viewModel.linkCount);
    ImGui::Separator();
    ImGui::Text("ワープ端点");
    if (viewModel.hasWarpPending) {
        ImGui::TextDisabled("端点を保留中(対になるnodeを選ぶと確定します)");
        if (ImGui::Button("保留キャンセル")) {
            ctx.action.CancelWarpPending();
        }
    }
    if (viewModel.hasWarpLink) {
        ImGui::Text("ワープ接続済み(相手: Scene %zu)", viewModel.warpPartnerScene + 1);
        if (ImGui::Button("ワープを削除")) {
            ctx.action.RemoveWarp();
        }
    }
    if (ImGui::Button("ワープ端点として保留")) {
        ctx.action.BeginWarpPending();
    }
    ImGui::Separator();
    ImGui::Text("edge結合");
    // LayerMismatchのみが拒否理由の場合はボタンを有効のままにし、確認ダイアログを
    // 1つ挟んでからallowLayerMismatch=trueで結合する。それ以外の拒否理由が
    // ある場合はボタンを無効化する。
    const bool mergeBlockedByOther =
        viewModel.mergeRefusal != rs::world::EdgeMergeRefusal::None &&
        !viewModel.mergeBlockedOnlyByLayerMismatch;
    ImGui::BeginDisabled(mergeBlockedByOther);
    if (ImGui::Button("edgeを結合(選択nodeで)")) {
        if (viewModel.mergeBlockedOnlyByLayerMismatch) {
            ImGui::OpenPopup("異なるレイヤーを結合しますか");
        } else {
            ctx.action.MergeEdges();
        }
    }
    ImGui::EndDisabled();
    if (mergeBlockedByOther) {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "結合不可: %s", EdgeMergeRefusalReasonJa(viewModel.mergeRefusal));
    }
    if (ImGui::BeginPopupModal("異なるレイヤーを結合しますか", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("異なるレイヤーのedgeを結合します。よろしいですか？");
        ImGui::Text("edge1のレイヤー: %s", rs::world::GuidToHex(viewModel.mergeEdgeFirstLayer).c_str());
        ImGui::Text("edge2のレイヤー: %s", rs::world::GuidToHex(viewModel.mergeEdgeSecondLayer).c_str());
        if (ImGui::Button("結合する")) {
            ctx.action.MergeEdges(/*allowLayerMismatch=*/true);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("キャンセル")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

} // namespace rs::app

//
// EditorPanelEdgeSection.cpp - 敷設エディタパネルの「選択edge プロパティ」UI
//
#include "EditorPanelEdgeSection.h"

#include <cstdio>
#include <optional>
#include <string>

#include "imgui.h"

#include "../action/TrackLayingText.h"

namespace rs::app {
namespace {

// 値がそろっていない欄に出す文字(入力欄の書式に渡すと、値の代わりに表示される)。
constexpr const char* kMixedValueText = "(複数の値)";

} // namespace

void DrawSelectedEdgeSection(const EdgeSectionContext& ctx) {
    const EdgePropertyViewModel viewModel = ctx.action.ViewModel();
    if (!viewModel.visible) {
        return;
    }

    if (viewModel.isCenterline) {
        // 複線中心線は実体を持たない(走行・押し出しの対象外)。cant/speedLimitは実レール側で
        // 編集し、trackPluginsは別パネルで編集できるため、ここには表示しない。
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "選択edge: 複線中心線");
        ImGui::TextDisabled("本数(TrackNum)=%d / 実レール%zu本の平均", viewModel.trackCount, viewModel.memberEdgeCount);
        ImGui::TextDisabled("cant/speedは編集不可(実体を持たないため)。プラグイン割当は「レール系プラグイン」パネルで編集");
        if (viewModel.hasTransitionPlan) {
            int comboIndex = viewModel.transitionComboIndex;
            bool changed = false;
            ImGui::SetNextItemWidth(140);
            if (ImGui::Combo("緩和曲線##centerline", &comboIndex,
                             kTransitionLabels.data(), kTransitionChoiceCount)) {
                changed = true;
            }
            float length = viewModel.transitionLength;
            if (comboIndex > 0) {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(80);
                if (ImGui::InputFloat("長さ[m]##centerline", &length, 0.0f, 0.0f, "%.1f",
                                      ImGuiInputTextFlags_EnterReturnsTrue)) {
                    changed = true;
                }
            }
            if (changed) {
                ctx.action.SetTransition(comboIndex, length);
            }
        }
        if (ImGui::Button("選択edgeを削除##centerline")) {
            ctx.action.RemoveSelectedEdge();
        }
        ImGui::TextDisabled("削除すると複線一括Pier/Pole配置と個別編集も消える(実レール本体は残る)");
        return;
    }

    // 複数選択中は、速度制限・カント自動・最大カント・削除だけを全レールに適用する。
    // 手動のカント値と緩和曲線は、レールごとの値なので無効にする。
    const bool multi = viewModel.selectedCount > 1;
    ImGui::Separator();
    if (multi) {
        ImGui::Text("選択中のレール %zu本", viewModel.selectedCount);
    } else {
        ImGui::Text("選択edge プロパティ");
    }
    float speedLimit = viewModel.speedLimit;
    if (ImGui::InputFloat("速度制限 [km/h](負=なし)", &speedLimit, 1.0f, 10.0f,
                          viewModel.speedLimitMixed ? kMixedValueText : "%.3f")) {
        ctx.action.SetSpeedLimit(speedLimit);
    }
    float cantAmm = static_cast<float>(viewModel.CantToMm(viewModel.cantA));
    float cantBmm = static_cast<float>(viewModel.CantToMm(viewModel.cantB));
    bool cantChanged = false;
    //自動モード。ONの間は曲線部の最大カントだけを指定し、手動の入力欄は無効表示にする。
    bool cantAuto = viewModel.cantAuto;
    float cantAutoMaxMm = static_cast<float>(viewModel.CantToMm(viewModel.cantAutoMax));
    // 自動のレールとそうでないレールが混ざっているときは、ラベルに(複数の値)を添える。
    // 押すと全レールが自動になる。
    if (ImGui::Checkbox(viewModel.cantAutoMixed ? "カント自動 (複数の値)##edge" : "カント自動##edge",
                        &cantAuto)) {
        if (cantAuto) {
            ctx.action.EnableCantAuto();
        } else {
            ctx.action.SetCantAuto(false);
        }
    }
    const bool anyCantAuto = viewModel.cantAuto || viewModel.cantAutoMixed;
    if (anyCantAuto) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120);
        const bool edited = ImGui::InputFloat("最大カント [mm]##edge", &cantAutoMaxMm, 1.0f, 10.0f,
                                              viewModel.cantAutoMaxMixed ? kMixedValueText : "%.3f");
        // 入力中は1文字ごとにコマンドを積まず、入力が終わった時点で1回だけ反映する。
        // +/-ボタンでの変更は入力欄がアクティブにならないので、その場で反映する。
        if (ImGui::IsItemDeactivatedAfterEdit() || (edited && !ImGui::IsItemActive())) {
            ctx.action.SetCantAutoMax(static_cast<float>(viewModel.CantFromMm(cantAutoMaxMm)));
        }
    }
    ImGui::BeginDisabled(anyCantAuto || multi);
    if (ImGui::InputFloat("カントA [mm]", &cantAmm, 1.0f, 10.0f)) {
        cantChanged = true;
    }
    if (ImGui::InputFloat("カントB [mm]", &cantBmm, 1.0f, 10.0f)) {
        cantChanged = true;
    }
    ImGui::EndDisabled();
    ImGui::BeginDisabled(anyCantAuto || multi);
    std::optional<float> cantMid;
    if (viewModel.hasCantMid) {
        float cantMidMm = static_cast<float>(viewModel.CantToMm(viewModel.cantMid));
        if (ImGui::InputFloat("カント(曲線部) [mm]", &cantMidMm, 1.0f, 10.0f)) {
            cantMid = static_cast<float>(viewModel.CantFromMm(cantMidMm));
            cantChanged = true;
        }
    }
    std::optional<float> cantTransitionRatio;
    float transitionRatioN = viewModel.cantTransitionRatio;
    if (ImGui::InputFloat("カント逓減率 1/N", &transitionRatioN, 1.0f, 10.0f)) {
        cantTransitionRatio = transitionRatioN;
        cantChanged = true;
    }
    ImGui::EndDisabled();
    if (cantChanged)
        ctx.action.SetCant(static_cast<float>(viewModel.CantFromMm(cantAmm)),
                           static_cast<float>(viewModel.CantFromMm(cantBmm)), cantMid, cantTransitionRatio);

    if (viewModel.hasTransitionPlan && !multi) {
        int comboIndex = viewModel.transitionComboIndex;
        bool changed = false;
        ImGui::SetNextItemWidth(140);
        if (ImGui::Combo("緩和曲線##edge", &comboIndex, kTransitionLabels.data(), kTransitionChoiceCount)) {
            changed = true;
        }
        float length = viewModel.transitionLength;
        if (comboIndex > 0) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80);
            // Enter確定のみ(逐次コミットでUndo履歴を汚さない)。
            if (ImGui::InputFloat("長さ[m]##edge", &length, 0.0f, 0.0f, "%.1f",
                                  ImGuiInputTextFlags_EnterReturnsTrue)) {
                changed = true;
            }
        }
        if (changed) {
            ctx.action.SetTransition(comboIndex, length);
        }
    }

    ImGui::Separator();
    ImGui::TextDisabled("edge分割: Altを押しながらedge上をクリック");
    if (ImGui::Button(multi ? "選択中のレールを削除" : "選択edgeを削除")) {
        ctx.action.RemoveSelectedEdge();
    }
    ImGui::BeginDisabled(multi || viewModel.pendingPlacementEdits == 0);
    if (ImGui::Button("手動で再配置(自動配置へ戻す)")) {
        ctx.action.ClearPlacementEdits();
    }
    ImGui::EndDisabled();
    if (viewModel.pendingPlacementEdits > 0) {
        ImGui::SameLine();
        ImGui::Text("実体化済み配置 %d件", viewModel.pendingPlacementEdits);
    }
}

} // namespace rs::app

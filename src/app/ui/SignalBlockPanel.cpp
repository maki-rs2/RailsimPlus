//
// SignalBlockPanel.cpp - 「信号・閉塞」パネル
//
#include "SignalBlockPanel.h"

#include "../../world/alignment/SpeedSignalRegistry.h"

#include <optional>
#include <string>
#include <vector>

#include "imgui.h"

namespace rs::app {

namespace {

// 信号1本の現示の表示・強制R・段の一覧と、段の追加・削除を描く。
void DrawSignalStages(const SignalBlockContext& ctx, const SignalBlockSignalRowViewModel& signal) {
    ImGui::TextUnformatted(signal.aspectText.c_str());
    // 中継・遠方は主信号に従うだけなので、手動の操作は無い。入換は強制Rだけ。
    const bool hasForcedRed = rs::world::IsBlockBoundarySignal(signal.kind) ||
                              signal.kind == rs::world::SpeedSignalKind::Shunting;
    if (hasForcedRed) {
        bool forcedRed = signal.forcedRed;
        if (ImGui::Checkbox("強制R", &forcedRed)) {
            ctx.action.SetSpeedSignalForceRed(signal.guid, forcedRed);
        }
    }
    // 仮の操作。在線または強制Rの赤でも、調整値の速度で進入できるようにする。
    if (rs::world::IsBlockBoundarySignal(signal.kind)) {
        ImGui::SameLine();
        bool entryPermitted = signal.entryPermitted;
        if (ImGui::Checkbox("進入許可", &entryPermitted)) {
            ctx.action.SetSpeedSignalEntryPermit(signal.guid, entryPermitted);
        }
    }
    ImGui::TextUnformatted("段:");
    for (size_t stageIndex = 0; stageIndex < signal.stages.size(); ++stageIndex) {
        const SignalBlockStageViewModel& stage = signal.stages[stageIndex];
        ImGui::PushID(static_cast<int>(stageIndex));
        ImGui::SameLine();
        ImGui::Text("%d km/h", stage.speedKmh);
        // 先頭の0の段は固定で、外せない。
        if (stage.speedKmh != 0) {
            ImGui::SameLine();
            if (ImGui::SmallButton("-")) {
                ctx.action.RemoveSpeedSignalStage(signal.guid, stage.speedKmh);
            }
        }
        ImGui::PopID();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    ImGui::BeginDisabled(signal.addableSpeeds.empty());
    if (ImGui::BeginCombo("段を追加", "+")) {
        for (int speedKmh : signal.addableSpeeds) {
            if (ImGui::Selectable((std::to_string(speedKmh) + " km/h").c_str(), false)) {
                ctx.action.AddSpeedSignalStage(signal.guid, speedKmh);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
}

} // namespace

void DrawSignalBlockPanel(const SignalBlockContext& ctx) {
    const SignalBlockViewModel viewModel = ctx.action.ViewModel();
    ImGui::Begin("信号・閉塞");

    ImGui::BeginDisabled(!viewModel.canCreateBlockFromSelection);
    if (ImGui::Button("閉塞にする")) {
        ctx.action.CreateBlockFromSelection();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    const bool pluginSelected = !viewModel.signalPluginFolder.empty();
    ImGui::BeginDisabled(!pluginSelected);
    if (ImGui::Button("信号を置く")) {
        ctx.action.ArmPlaceSpeedSignal();
    }
    ImGui::EndDisabled();
    if (!pluginSelected) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "%s", kSignalPluginNotSelectedText);
    }
    // 信号プラグイン(station.jsonに`signal`の速度表を持つStation)。信号を置くには選択が要る。
    ImGui::Text("信号プラグイン: %s",
                pluginSelected ? viewModel.signalPluginName.c_str() : "未選択");
    if (const std::optional<std::string> picked =
            ctx.pathPicker.Field("フォルダ##signalplugin", viewModel.signalPluginFolder,
                                 FolderPick("信号プラグインのフォルダを選択"))) {
        ctx.action.SelectSignalPlugin(*picked);
    }
    if (!viewModel.signalPluginError.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", viewModel.signalPluginError.c_str());
    }
    ImGui::TextUnformatted("向き: 最後に置いた信号・駅舎に合わせる。Shiftを押している間だけ逆");

    ImGui::Separator();
    if (viewModel.armedAction != rs::editor::SignalBlockArmedAction::None) {
        const char* armedText = "";
        switch (viewModel.armedAction) {
        case rs::editor::SignalBlockArmedAction::PlaceSpeedSignal:
            armedText = "ビューポートで信号を置く位置をクリックしてください(Escで解除)";
            break;
        case rs::editor::SignalBlockArmedAction::None:
            break;
        }
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "%s", armedText);
        if (!viewModel.placeBlockedMessage.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", viewModel.placeBlockedMessage.c_str());
        }
    }
    if (viewModel.selectedBlock.visible) {
        ImGui::Text("選択中の閉塞: %zu edge", viewModel.selectedBlock.edgeCount);
        if (ImGui::Button("配置を解除")) {
            ctx.action.Disarm();
        }
        ImGui::PushID("signals");
        for (size_t index = 0; index < viewModel.selectedBlock.signals.size(); ++index) {
            const SignalBlockSignalRowViewModel& signal = viewModel.selectedBlock.signals[index];
            ImGui::PushID(static_cast<int>(index));
            ImGui::Text("信号 向き: %s", signal.direction > 0 ? "順" : "逆");
            if (!signal.pluginName.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", signal.pluginName.c_str());
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("信号を削除")) {
                ctx.action.RemoveSpeedSignal(signal.guid);
                ImGui::PopID();
                break;
            }
            DrawSignalStages(ctx, signal);
            ImGui::PopID();
        }
        ImGui::PopID();
    }

    // 閉塞に属さない表示専用の信号。主信号の現示に従うだけで、列車の速度には影響しない。
    if (!viewModel.displayOnlySignals.empty()) {
        ImGui::Separator();
        ImGui::TextUnformatted("表示専用の信号");
        ImGui::PushID("displayOnly");
        for (size_t index = 0; index < viewModel.displayOnlySignals.size(); ++index) {
            const SignalBlockSignalRowViewModel& signal = viewModel.displayOnlySignals[index];
            ImGui::PushID(static_cast<int>(index));
            ImGui::Text("%s 向き: %s", SpeedSignalKindText(signal.kind), signal.direction > 0 ? "順" : "逆");
            ImGui::SameLine();
            if (ImGui::SmallButton("信号を削除")) {
                ctx.action.RemoveSpeedSignal(signal.guid);
                ImGui::PopID();
                break;
            }
            DrawSignalStages(ctx, signal);
            ImGui::PopID();
        }
        ImGui::PopID();
    }

    ImGui::End();
}

} // namespace rs::app

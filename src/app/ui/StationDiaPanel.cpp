#include "StationDiaPanel.h"

#include <algorithm>
#include <cstring>
#include <utility>
#include <vector>

#include "imgui.h"

#include "../scene/SceneRenderer.h"
#include "StationDiaPointSection.h"

namespace rs::app {
namespace {

void CopyName(char* destination, size_t size, const std::string& value) {
    if (size == 0) return;
    const size_t copyLength = std::min(value.size(), size - 1);
    std::memcpy(destination, value.data(), copyLength);
    destination[copyLength] = '\0';
}

} // namespace

void DrawStationDiaPanel(const StationDiaContext& ctx) {
    StationDiaViewModel viewModel = ctx.action.ViewModel();
    ImGui::Begin("駅・ダイヤ");
    if (ImGui::Button("駅を追加")) {
        ctx.action.AddStation();
    }
    ImGui::SameLine();
    if (ImGui::Button("番線を追加")) {
        ctx.action.AddPlatform();
    }
    ctx.action.BeginHoverFrame();
    for (std::size_t rowIndex = 0; rowIndex < viewModel.stations.size(); ++rowIndex) {
        const StationDiaStationRowViewModel& station = viewModel.stations[rowIndex];
        const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen |
            (station.selected ? ImGuiTreeNodeFlags_Selected : 0);
        // ImGuiのIDはViewModelのコピー先アドレスに依存させない(毎フレーム変わると
        // ツリーの開閉状態が保てない)。行位置で固定する。
        ImGui::PushID(static_cast<int>(rowIndex));
        const bool open = ImGui::TreeNodeEx("##station", flags, "%s", station.name.c_str());
        if (ImGui::IsItemHovered()) {
            ctx.action.SetHoveredStation(station.id);
        }
        if (ImGui::IsItemClicked()) {
            ctx.action.SelectStation(station.id);
        }
        ImGui::SameLine();
        const bool removeStation = ImGui::SmallButton("削除##station");
        if (removeStation) {
            ctx.action.RemoveStation(station.id);
            ImGui::PopID();
            break;
        }
        if (open) {
            for (const StationDiaPlatformRowViewModel& platform : station.platforms) {
                if (ImGui::Selectable(platform.name.c_str(), platform.selected)) {
                    ctx.action.SelectPlatform(platform.id);
                }
                if (ImGui::IsItemHovered()) {
                    ctx.action.SetHoveredPlatform(platform.id);
                }
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    for (const StationDiaPlatformRowViewModel& platform : viewModel.unassignedPlatforms) {
        if (ImGui::Selectable(("(未所属) " + platform.name).c_str(), platform.selected)) {
            ctx.action.SelectPlatform(platform.id);
        }
        if (ImGui::IsItemHovered()) {
            ctx.action.SetHoveredPlatform(platform.id);
        }
    }
    DrawStationDiaPointSection(ctx.action, viewModel.points);
    if (viewModel.selectedStation.visible) {
        ImGui::Separator();
        if (ctx.state.stationBufferId != viewModel.selectedStation.id) {
            // 選択中の駅が切り替わったら編集バッファへ現在の駅名を読み込み直す。
            ctx.state.stationBufferId = viewModel.selectedStation.id;
            CopyName(ctx.state.stationName, sizeof(ctx.state.stationName), viewModel.selectedStation.name);
        }
        if (ImGui::InputText("駅名", ctx.state.stationName, sizeof(ctx.state.stationName), ImGuiInputTextFlags_EnterReturnsTrue)) {
            ctx.action.RenameStation(viewModel.selectedStation.id, ctx.state.stationName);
        }
    }
    if (viewModel.selectedPlatform.visible) {
        const StationDiaSelectedPlatformViewModel& platform = viewModel.selectedPlatform;
        ImGui::Separator();
        ImGui::TextUnformatted("選択中の番線");
        if (ctx.state.platformBufferId != platform.id) {
            // 選択中の番線が切り替わったら編集バッファへ現在の番線名を読み込み直す。
            ctx.state.platformBufferId = platform.id;
            CopyName(ctx.state.platformName, sizeof(ctx.state.platformName), platform.name);
        }
        if (ImGui::InputText("番線名", ctx.state.platformName, sizeof(ctx.state.platformName), ImGuiInputTextFlags_EnterReturnsTrue)) {
            ctx.action.RenameSelectedPlatform(ctx.state.platformName);
        }
        bool stoppable = platform.stoppable;
        if (ImGui::Checkbox("停車可能", &stoppable)) {
            ctx.action.SetSelectedPlatformStoppable(stoppable);
        }
        const char* stopReferencePreview = "中央";
        if (platform.stopReference == rs::world::StopReference::Front) {
            stopReferencePreview = "前基準";
        } else if (platform.stopReference == rs::world::StopReference::Rear) {
            stopReferencePreview = "後ろ基準";
        }
        if (ImGui::BeginCombo("停止位置基準", stopReferencePreview)) {
            if (ImGui::Selectable("前基準", platform.stopReference == rs::world::StopReference::Front)) {
                ctx.action.SetSelectedPlatformStopReference(rs::world::StopReference::Front);
            }
            if (ImGui::Selectable("中央", platform.stopReference == rs::world::StopReference::Center)) {
                ctx.action.SetSelectedPlatformStopReference(rs::world::StopReference::Center);
            }
            if (ImGui::Selectable("後ろ基準", platform.stopReference == rs::world::StopReference::Rear)) {
                ctx.action.SetSelectedPlatformStopReference(rs::world::StopReference::Rear);
            }
            ImGui::EndCombo();
        }
        double stopOffsetM = platform.stopOffsetM;
        if (ImGui::InputDouble("オフセット(m)", &stopOffsetM, 0.0, 0.0, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue)) {
            ctx.action.SetSelectedPlatformStopOffset(stopOffsetM);
        }
        // 「次発車」とタグ定義は描画側の実行時状態から取る(PlatformRoutineEnvironment)。
        PlatformRoutineEnvironment routineEnvironment;
        if (ctx.sceneRenderer != nullptr) {
            const SceneRenderer* sceneRenderer = ctx.sceneRenderer;
            routineEnvironment.cursorIndex =
                [sceneRenderer](const rs::world::Guid& targetPlatform,
                                const std::vector<rs::world::PlatformRoutineElement>& routine,
                                int layerKey, rs::world::TrainId trainId) {
                    return sceneRenderer->PlatformRoutineCursorIndex(targetPlatform, routine, layerKey,
                                                                     trainId);
                };
            routineEnvironment.tagChoices = [sceneRenderer]() {
                std::vector<TagChoiceViewModel> choices;
                for (const rs::world::TrainTagDef& tagDef : sceneRenderer->TrainTagDefs()) {
                    choices.push_back({tagDef.id, tagDef.label});
                }
                return choices;
            };
        }
        PlatformRoutineAction routineAction(ctx.editor, platform.id, std::move(routineEnvironment));
        DrawPlatformRoutinePanel({routineAction, ctx.state.platformRoutine,
                                  ctx.trainPlacementUi.placedTrainIds});
        const char* stationPreview = platform.stationName.empty() ? "未所属" : platform.stationName.c_str();
        if (ImGui::BeginCombo("所属駅", stationPreview)) {
            if (ImGui::Selectable("未所属", platform.stationId == rs::world::NilGuid())) {
                ctx.action.SetSelectedPlatformStation(rs::world::NilGuid());
            }
            for (const StationDiaStationRowViewModel& station : viewModel.stations) {
                if (ImGui::Selectable(station.name.c_str(), platform.stationId == station.id)) {
                    ctx.action.SetSelectedPlatformStation(station.id);
                }
            }
            ImGui::EndCombo();
        }
        ImGui::Text("edge数: %d（ビューポートでクリックして追加・除去）", platform.edgeCount);
        if (ImGui::Button("番線を削除")) {
            ctx.action.RemoveSelectedPlatform();
        }
    }
    ImGui::End();
}

} // namespace rs::app

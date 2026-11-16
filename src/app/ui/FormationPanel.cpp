//
// FormationPanel.cpp
//
#include "FormationPanel.h"

#include <cstdio>
#include <optional>

#include "imgui.h"

#include "../action/FormationAction.h"
#include "../plugin_session/PluginSelector.h"
#include "../../sim/TrainKinematics.h"
#include "../../platform/file/ShellFileTypes.h"
#include "UiPanelHelpers.h"

namespace rs::app {

namespace {

// 「編成の車両」ウィンドウ: 選択中の編成の車両を順に列挙し、表示/反転/順序入替/削除を
// 行う。選択編成が無ければ何も描画しない(タイトルはImGuiのID安定のため固定)。
void DrawFormationCarsPanel(const FormationPanelContext& ctx, const FormationViewModel& viewModel) {
    if (!viewModel.hasSelectedDef) {
        return;
    }

    ImGui::SetNextWindowPos(ImVec2(380, 420), ImGuiCond_FirstUseEver);
    ImGui::Begin("編成の車両");
    ImGui::Text("編成: %s", viewModel.selectedDefName.c_str());
    ImGui::Separator();

    for (std::size_t carIndex = 0; carIndex < viewModel.selectedCars.size(); ++carIndex) {
        const FormationCarViewModel& car = viewModel.selectedCars[carIndex];
        ImGui::PushID(static_cast<int>(carIndex));
        ImGui::Text("%zu両目: %s", carIndex + 1, car.displayName.c_str());
        ImGui::SameLine();
        bool reversed = car.reversed;
        if (ImGui::Checkbox("反転", &reversed)) {
            ctx.action.SetCarReversed(carIndex, reversed);
        }
        ImGui::SameLine();
        if (ImGui::Button("▲")) {
            ctx.action.MoveCar(carIndex, -1);
        }
        ImGui::SameLine();
        if (ImGui::Button("▼")) {
            ctx.action.MoveCar(carIndex, +1);
        }
        ImGui::SameLine();
        if (ImGui::Button("削除")) {
            ctx.action.RemoveCar(carIndex);
        }
        ImGui::PopID();
    }
    ImGui::End();
}

// 配置済みグループの走行コントロール(目標速度)。軸/台車chase解決は
// SceneRenderer::Tick が毎フレーム行うので、ここでは目標速度を設定するだけ。
void DrawPlacedGroupsSection(const FormationPanelContext& ctx, const FormationViewModel& viewModel) {
    if (!viewModel.placedGroups.empty()) {
        ImGui::Separator();
        ImGui::TextUnformatted("配置済み編成の走行");
        for (std::size_t groupIndex = 0; groupIndex < viewModel.placedGroups.size(); ++groupIndex) {
            const PlacedGroupViewModel& group = viewModel.placedGroups[groupIndex];
            ImGui::PushID(static_cast<int>(groupIndex));
            ImGui::Text("編成 #%zu", groupIndex + 1);
            for (std::size_t tagIndex = 0; tagIndex < viewModel.tags.size(); ++tagIndex) {
                const TrainTagViewModel& tag = viewModel.tags[tagIndex];
                bool assigned = group.tagAssigned[tagIndex];
                // タグは配置元の編成が持つ。配置元の無い列車では触れない。
                const bool hasSourceFormation = !rs::world::GuidIsNil(group.sourceFormationId);
                ImGui::BeginDisabled(!hasSourceFormation);
                if (ImGui::Checkbox(tag.label.c_str(), &assigned)) {
                    ctx.action.SetFormationTagAssigned(group.sourceFormationId, tag.id, assigned);
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
            }
            if (!viewModel.tags.empty()) {
                ImGui::NewLine();
            }
            float targetKmh = group.targetSpeedKmh;
            // 負値=後退方向への目標速度。前進・後退で同じ最高速を使う
            // (RailSim II にも別値の概念は無い)。
            if (ImGui::SliderFloat("目標速度[km/h]", &targetKmh,
                                   -rs::sim::TrainKinematics::kMaxTargetSpeedKmh,
                                   rs::sim::TrainKinematics::kMaxTargetSpeedKmh)) {
                ctx.action.SetGroupTargetSpeedKmh(group.trainId, targetKmh);
            }
            ImGui::SameLine();
            ImGui::Text("現在=%.1fkm/h", group.currentSpeedKmh);
            ImGui::SameLine();
            if (ImGui::Button("削除##group")) {
                ctx.action.RemoveGroup(group.trainId);
            }

            // 運転台視点トグル。
            bool cabinViewActive = group.cabinViewActive;
            if (ImGui::Checkbox("運転台視点", &cabinViewActive)) {
                ctx.action.SetCabinViewActive(group.trainId, cabinViewActive);
            }

            // 自動停止の手動テスト用UI。動作確認用で、距離は毎回このパネルから
            // 直接指定する。
            // 入力値は編成ごとに持つ(1つの値を共有すると、ある編成で入れた値が
            // 全編成の表示に化ける)。
            float& stopDistanceInput = ctx.state.stopDistanceInputs.try_emplace(group.trainId, 50.0f).first->second;
            ImGui::InputFloat("停止距離[m]##stopdist", &stopDistanceInput, 1.0f, 10.0f, "%.1f");
            ImGui::SameLine();
            if (ImGui::Button("自動停止設定")) {
                ctx.action.SetGroupStopDistance(group.trainId, stopDistanceInput);
            }
            ImGui::SameLine();
            if (ImGui::Button("自動停止解除")) {
                ctx.action.ClearGroupStopTarget(group.trainId);
            }
            ImGui::SameLine();
            if (ImGui::Button("強制停止")) {
                ctx.action.ForceStopGroup(group.trainId);
            }

            // 運行状態はダイヤ状態機械(rs::sim::DiaRuntime)が唯一の情報源。
            if (group.diaState != rs::sim::DiaState::Running) {
                ImGui::SameLine();
                const char* stateLabel = "減速中";
                if (group.diaState == rs::sim::DiaState::Stopped) {
                    stateLabel = "停車中";
                } else if (group.diaState == rs::sim::DiaState::Departing) {
                    stateLabel = "発車直前";
                }
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s", stateLabel);
                if (group.hasDeparture) {
                    const int secondsOfDay = group.departureSecondsOfDay;
                    ImGui::SameLine();
                    ImGui::Text("発車 %02d:%02d:%02d", secondsOfDay / 3600, (secondsOfDay / 60) % 60,
                                secondsOfDay % 60);
                }
            }
            ImGui::PopID();
        }
    }
}

} // namespace

void DrawFormationListPanel(const FormationPanelContext& ctx) {
    const FormationViewModel viewModel = ctx.action.ViewModel();
    ImGui::Begin("編成");
    DrawAddRow(ctx.selector, ctx.pathPicker, ctx.state.lastError, "train");

    if (ImGui::Button("新規編成")) {
        ctx.action.AddFormation();
    }

    // 編成テンプレート(TrainGroupTemplate\*.txt)一括読込: 新規編成として追加し、
    // 各TrainTemplateをAddPathでその編成へ追加、Reverse=yesならSetFormationCarReversedで
    // 反映する(TrainGroupTemplateApplier.h参照。重複追加は編成の既存仕様通り許容)。
    ImGui::TextUnformatted("編成テンプレート読込(TrainGroupTemplate)");
    if (const std::optional<std::string> picked = ctx.pathPicker.Button(
            "ファイルを選んで読込…##traingrouptemplate",
            FilePick("編成テンプレートを選択", rs::platform_file::kTextFileType))) {
        ctx.state.templateLastError = ctx.action.ApplyTrainGroupTemplate(*picked)
            ? std::string()
            : "テンプレートとして読めませんでした(ファイルの形式を確認してください)";
    }
    if (!ctx.state.templateLastError.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", ctx.state.templateLastError.c_str());
    }
    ImGui::Separator();

    // 編成リスト(複数編成の作り分け+選択)。
    ImGui::TextUnformatted("編成一覧");
    std::size_t pendingRemove = static_cast<std::size_t>(-1);
    for (std::size_t rowIndex = 0; rowIndex < viewModel.defs.size(); ++rowIndex) {
        const FormationRowViewModel& def = viewModel.defs[rowIndex];
        ImGui::PushID(static_cast<int>(rowIndex));
        std::string label = def.name + " (" + std::to_string(def.carCount) + "両)";
        if (ImGui::Selectable(label.c_str(), def.selected)) {
            ctx.action.SelectFormation(rowIndex);
        }
        ImGui::SameLine();
        if (ImGui::Button("削除##def")) {
            pendingRemove = rowIndex;
        }
        ImGui::PopID();
    }
    if (pendingRemove != static_cast<std::size_t>(-1)) {
        ctx.action.RemoveFormation(pendingRemove);
    }

    if (viewModel.hasSelectedDef) {
        char nameBuf[128];
        std::snprintf(nameBuf, sizeof(nameBuf), "%s", viewModel.selectedDefName.c_str());
        if (ImGui::InputText("編成名", nameBuf, sizeof(nameBuf))) {
            ctx.action.SetSelectedFormationName(nameBuf);
        }
    }
    ImGui::Separator();

    // グルーピング配置: 実際の配置操作はビューポート側(TrainPlacementInput)の
    // マウスオーバー+左クリックで行う。ここでは反転指定のみを
    // 提供する(反転値はuiStateを介してTrainPlacementInputが読む)。
    bool placementReversedHead = viewModel.placementReversedHead;
    if (ImGui::Checkbox("反転(dir=-1)##train", &placementReversedHead)) {
        ctx.action.SetPlacementReversedHead(placementReversedHead);
    }
    if (viewModel.selectedCars.empty()) {
        ImGui::TextDisabled("(選択中の編成に車両がありません。上の追加行から車両を追加してください)");
    } else {
        ImGui::TextDisabled("(軌道にマウスを合わせて左クリックで配置 / 配置済み編成をクリック選択してDeleteで撤去)");
    }

    ImGui::Separator();
    ImGui::TextUnformatted("属性タグ");
    ImGui::InputText("タグ名", ctx.state.newTagLabel, sizeof(ctx.state.newTagLabel));
    ImGui::SameLine();
    if (ImGui::Button("タグを追加")) {
        ctx.action.AddTrainTag(ctx.state.newTagLabel);
        ctx.state.newTagLabel[0] = '\0';
    }
    for (const TrainTagViewModel& tag : viewModel.tags) {
        ImGui::PushID(tag.id);
        ImGui::Text("%s (id: %d)", tag.label.c_str(), tag.id);
        ImGui::SameLine();
        if (ImGui::Button("削除##tag")) {
            ctx.action.RemoveTrainTag(tag.id);
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }

    DrawPlacedGroupsSection(ctx, viewModel);

    ImGui::End();

    DrawFormationCarsPanel(ctx, viewModel);
}

} // namespace rs::app

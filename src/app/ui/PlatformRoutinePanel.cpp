#include "PlatformRoutinePanel.h"

#include <algorithm>
#include <cstdio>
#include <string>

#include "imgui.h"

namespace rs::app {
namespace {

const char* ActionLabel(rs::world::DiaAction action) {
    switch (action) {
    case rs::world::DiaAction::Stop: return "停車";
    case rs::world::DiaAction::Reverse: return "折返";
    case rs::world::DiaAction::Pass: return "通過";
    }
    return "不明";
}

const char* TimeTypeLabel(rs::world::DiaTimeType timeType) {
    return timeType == rs::world::DiaTimeType::AbsoluteTimeOfDay ? "時刻指定発車" : "経過時間発車";
}

std::string TrainLabel(const std::vector<rs::world::TrainId>& groupIds,
                         const rs::world::TrainId& trainId) {
    const std::vector<rs::world::TrainId>::const_iterator iterator =
        std::find(groupIds.begin(), groupIds.end(), trainId);
    if (iterator == groupIds.end()) {
        return "編成（未配置）";
    }
    const size_t displayNumber = static_cast<size_t>(iterator - groupIds.begin()) + 1;
    return "編成 #" + std::to_string(displayNumber);
}

void DrawNextDeparture(const RoutineLayerViewModel& layer) {
    if (layer.nextDepartureIndex < 0) {
        ImGui::TextDisabled("有効な要素がありません");
    } else {
        ImGui::Text("次発車: 要素 %d", layer.nextDepartureIndex + 1);
    }
}

void DrawRoutineElements(const PlatformRoutineContext& ctx, const RoutineLayerRef& layer, const RoutineLayerViewModel& routine) {
    for (std::size_t elementIndex = 0; elementIndex < routine.elements.size(); ++elementIndex) {
        const RoutineElementViewModel& element = routine.elements[elementIndex];
        ImGui::PushID(static_cast<int>(elementIndex));
        char label[64];
        std::snprintf(label, sizeof(label), "要素 %zu%s", elementIndex + 1, element.enabled ? "" : " (無効)");
        if (ImGui::CollapsingHeader(label)) {
            bool enabled = element.enabled;
            if (ImGui::Checkbox("有効", &enabled)) {
                ctx.action.SetElementEnabled(layer, elementIndex, enabled);
            }
            if (ImGui::BeginCombo("動作", ActionLabel(element.action))) {
                const rs::world::DiaAction actions[] = {rs::world::DiaAction::Stop, rs::world::DiaAction::Reverse, rs::world::DiaAction::Pass};
                for (rs::world::DiaAction candidate : actions) {
                    if (ImGui::Selectable(ActionLabel(candidate), element.action == candidate)) {
                        ctx.action.SetElementAction(layer, elementIndex, candidate);
                    }
                }
                ImGui::EndCombo();
            }
            if (ImGui::BeginCombo("発車時刻種別", TimeTypeLabel(element.timeType))) {
                const rs::world::DiaTimeType types[] = {rs::world::DiaTimeType::ElapsedFromArrival, rs::world::DiaTimeType::AbsoluteTimeOfDay};
                for (rs::world::DiaTimeType candidate : types) {
                    if (ImGui::Selectable(TimeTypeLabel(candidate), element.timeType == candidate)) {
                        ctx.action.SetElementTimeType(layer, elementIndex, candidate);
                    }
                }
                ImGui::EndCombo();
            }
            if (element.timeType == rs::world::DiaTimeType::ElapsedFromArrival) {
                double dwellSeconds = element.dwellSeconds;
                if (ImGui::InputDouble("停車秒数", &dwellSeconds, 0.0, 0.0, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue)) {
                    ctx.action.SetElementDwellSeconds(layer, elementIndex, dwellSeconds);
                }
            } else {
                for (std::size_t timeIndex = 0; timeIndex < element.departureTimes.size(); ++timeIndex) {
                    const rs::world::DiaTimeOfDay& time = element.departureTimes[timeIndex];
                    ImGui::PushID(static_cast<int>(timeIndex));
                    int hour = time.hour;
                    int minute = time.minute;
                    int second = time.second;
                    const bool changed = ImGui::InputInt("時", &hour, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue) | ImGui::InputInt("分", &minute, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue) | ImGui::InputInt("秒", &second, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("時刻を削除")) {
                        ctx.action.RemoveDepartureTime(layer, elementIndex, timeIndex);
                    } else if (changed) {
                        ctx.action.SetDepartureTime(layer, elementIndex, timeIndex, hour, minute, second);
                    }
                    ImGui::PopID();
                }
                if (ImGui::Button("時刻を追加")) {
                    ctx.action.AddDepartureTime(layer, elementIndex);
                }
            }
            if (ImGui::BeginTable("SwitchChanges", 3)) {
                for (std::size_t switchIndex = 0; switchIndex < element.switchChanges.size(); ++switchIndex) {
                    const rs::world::SwitchChange& change = element.switchChanges[switchIndex];
                    ImGui::PushID(static_cast<int>(switchIndex));
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    char switchName[128];
                    std::snprintf(switchName, sizeof(switchName), "%s", change.switchName.c_str());
                    const bool nameChanged = ImGui::InputText("スイッチ名", switchName, sizeof(switchName), ImGuiInputTextFlags_EnterReturnsTrue);
                    ImGui::TableSetColumnIndex(1);
                    int value = change.value;
                    const bool valueChanged = ImGui::InputInt("値", &value, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue);
                    ImGui::TableSetColumnIndex(2);
                    if (ImGui::SmallButton("削除##switch")) {
                        ctx.action.RemoveSwitchChange(layer, elementIndex, switchIndex);
                    } else if (nameChanged || valueChanged) {
                        ctx.action.SetSwitchChange(layer, elementIndex, switchIndex, switchName, value);
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            if (ImGui::Button("スイッチ変更を追加")) {
                ctx.action.AddSwitchChange(layer, elementIndex);
            }
            if (ImGui::Button("削除")) {
                ctx.action.RemoveElement(layer, elementIndex);
            }
            ImGui::SameLine();
            if (element.canMoveUp && ImGui::Button("上へ")) {
                ctx.action.MoveElement(layer, elementIndex, -1);
            }
            ImGui::SameLine();
            if (element.canMoveDown && ImGui::Button("下へ")) {
                ctx.action.MoveElement(layer, elementIndex, 1);
            }
        }
        ImGui::PopID();
    }
    if (ImGui::Button("要素を追加")) {
        ctx.action.AddElement(layer);
    }
}

} // namespace

void DrawPlatformRoutinePanel(const PlatformRoutineContext& ctx) {
    const PlatformRoutineViewModel viewModel = ctx.action.ViewModel();
    if (!viewModel.visible) {
        return;
    }
    ImGui::Separator();
    if (!ImGui::BeginTabBar("RoutineLayers")) {
        return;
    }
    if (ImGui::BeginTabItem("全列車共通")) {
        ImGui::TextUnformatted("全列車共通ルーチン表");
        DrawNextDeparture(viewModel.common);
        DrawRoutineElements(ctx, {}, viewModel.common);
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("編成ごと")) {
        for (std::size_t index = 0; index < viewModel.trainLayers.size(); ++index) {
            const TrainRoutineLayerViewModel& entry = viewModel.trainLayers[index];
            const std::string label = TrainLabel(ctx.placedTrainIds, entry.trainId);
            ImGui::PushID(static_cast<const void*>(entry.trainId.data()));
            if (ImGui::CollapsingHeader(label.c_str())) {
                const RoutineLayerRef layer{RoutineLayerRef::Kind::Train, index};
                DrawNextDeparture(entry.layer);
                DrawRoutineElements(ctx, layer, entry.layer);
                if (ImGui::Button("編成ブロックを削除")) {
                    ctx.action.RemoveTrainRoutine(index);
                }
            }
            ImGui::PopID();
        }
        if (ctx.placedTrainIds.empty()) {
            ImGui::TextDisabled("配置済みの編成がありません");
        } else {
            const std::string preview = rs::world::GuidIsNil(ctx.state.trainIdToAdd)
                ? "編成を選択"
                : TrainLabel(ctx.placedTrainIds, ctx.state.trainIdToAdd);
            if (ImGui::BeginCombo("編成", preview.c_str())) {
                for (const rs::world::TrainId& trainId : ctx.placedTrainIds) {
                    const bool selected = trainId == ctx.state.trainIdToAdd;
                    const std::string optionLabel = TrainLabel(ctx.placedTrainIds, trainId);
                    if (ImGui::Selectable(optionLabel.c_str(), selected)) {
                        ctx.state.trainIdToAdd = trainId;
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            if (ImGui::Button("編成を追加") &&
                !rs::world::GuidIsNil(ctx.state.trainIdToAdd) &&
                !ctx.action.AddTrainRoutine(ctx.state.trainIdToAdd)) {
                ImGui::TextDisabled("選択した編成は既にあります");
            }
        }
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("タグごと")) {
        for (std::size_t index = 0; index < viewModel.tagLayers.size(); ++index) {
            const TagRoutineLayerViewModel& entry = viewModel.tagLayers[index];
            ImGui::PushID(static_cast<int>(index));
            char label[256];
            if (entry.hasTagDef) {
                std::snprintf(label, sizeof(label), "タグ: %s (id: %d)", entry.tagLabel.c_str(), entry.tagId);
            } else {
                std::snprintf(label, sizeof(label), "タグ: (不明 id=%d)", entry.tagId);
            }
            if (ImGui::CollapsingHeader(label)) {
                const RoutineLayerRef layer{RoutineLayerRef::Kind::Tag, index};
                DrawNextDeparture(entry.layer);
                DrawRoutineElements(ctx, layer, entry.layer);
                if (ImGui::Button("タグ設定を削除")) {
                    ctx.action.RemoveTagRoutine(index);
                }
                ImGui::SameLine();
                if (entry.canMoveUp && ImGui::Button("上へ")) {
                    ctx.action.MoveTagRoutine(index, -1);
                }
                ImGui::SameLine();
                if (entry.canMoveDown && ImGui::Button("下へ")) {
                    ctx.action.MoveTagRoutine(index, 1);
                }
            }
            ImGui::PopID();
        }
        if (viewModel.tagChoices.empty()) {
            ImGui::TextDisabled("利用可能なタグ定義がありません");
        } else {
            const TagChoiceViewModel* selectedTag = nullptr;
            for (const TagChoiceViewModel& tag : viewModel.tagChoices) {
                if (tag.id == ctx.state.tagIdToAdd) {
                    selectedTag = &tag;
                }
            }
            const char* preview = selectedTag == nullptr ? "タグを選択" : selectedTag->label.c_str();
            if (ImGui::BeginCombo("対象タグ", preview)) {
                for (const TagChoiceViewModel& tag : viewModel.tagChoices) {
                    const bool selected = tag.id == ctx.state.tagIdToAdd;
                    if (ImGui::Selectable(tag.label.c_str(), selected)) {
                        ctx.state.tagIdToAdd = tag.id;
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            if (ImGui::Button("タグ設定を追加") && ctx.state.tagIdToAdd >= 0 && !ctx.action.AddTagRoutine(ctx.state.tagIdToAdd)) {
                ImGui::TextDisabled("指定したタグ設定は既にあります");
            }
        }
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
}

} // namespace rs::app

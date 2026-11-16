#include "InputSettingsRows.h"

#include <array>
#include <iterator>

#include <magic_enum/magic_enum.hpp>

namespace rs::app {
namespace {

struct RowLabels {
    input::InputAction action;
    // 組の先頭の行だけ見出しの翻訳キーを持つ(2行目以降は空)。
    const char* sectionKey;
    const char* operationKey;
};

constexpr RowLabels kRowLabels[] = {
    {input::InputAction::CameraMoveForward, "game_settings.input.camera", "game_settings.input.forward"},
    {input::InputAction::CameraMoveBack, "", "game_settings.input.back"},
    {input::InputAction::CameraMoveLeft, "", "game_settings.input.left"},
    {input::InputAction::CameraMoveRight, "", "game_settings.input.right"},
    {input::InputAction::EditorUndo, "game_settings.input.edit", "game_settings.input.undo"},
    {input::InputAction::EditorRedo, "", "game_settings.input.redo"},
    {input::InputAction::EditorDelete, "", "game_settings.input.delete"},
    {input::InputAction::CameraRollLeft, "game_settings.input.camera", "game_settings.input.roll_left"},
    {input::InputAction::CameraRollRight, "", "game_settings.input.roll_right"},
    {input::InputAction::CameraRollReset, "", "game_settings.input.roll_reset"},
};

constexpr bool LabelsMatchRebindableActions() {
    if (std::size(kRowLabels) != std::size(input::kRebindableActions)) return false;
    for (std::size_t i = 0; i < std::size(kRowLabels); ++i) {
        if (kRowLabels[i].action != input::kRebindableActions[i].action) return false;
    }
    return true;
}
static_assert(LabelsMatchRebindableActions());

// 画面の重なり判定でのコンテキスト名。Uiは常に有効で、どのモードとも同時に使うのでcommon。
constexpr std::array<const char*, magic_enum::enum_count<input::InputContext>()> kContextNames = {
    "common", "edit", "drive", "view", "photo"};

const char* ContextName(input::InputContext context) { return kContextNames[static_cast<std::size_t>(context)]; }

std::string ChordOf(const input::InputBinding& binding) {
    return input::FormatKeyChord({binding.key, {binding.ctrl, binding.shift, binding.alt}});
}

} // namespace

void FillInputSettingsRows(workspace::InputSettingsPresentation& presentation,
                           const std::vector<input::InputBinding>& bindings, const Localization& localization) {
    presentation.rows_.clear();
    for (const RowLabels& labels : kRowLabels) {
        workspace::InputSettingsRow row;
        if (labels.sectionKey[0] != '\0') row.section_ = localization.Text(labels.sectionKey);
        row.operation_ = localization.Text(labels.operationKey);
        if (const std::optional<input::InputBinding> primary = input::FindPrimaryBinding(bindings, labels.action)) {
            row.context_ = ContextName(primary->context);
            if (primary->key != input::InputKey::None) row.binding_ = ChordOf(*primary);
        }
        const input::RebindableAction* entry = input::FindRebindableAction(labels.action);
        row.ctrlDash_ = entry != nullptr && entry->ctrlDashFollows;
        presentation.rows_.push_back(std::move(row));
    }
    // 言語の切り替えなどで作り直しても、入力待ちの行は待ったままにする。
    if (presentation.waitingRow_ >= 0 && static_cast<std::size_t>(presentation.waitingRow_) < presentation.rows_.size()) {
        presentation.rows_[static_cast<std::size_t>(presentation.waitingRow_)].waiting_ = true;
    }
    presentation.fixed_.clear();
    for (const input::InputBinding& binding : bindings) {
        if (input::FindRebindableAction(binding.action) != nullptr) continue;
        presentation.fixed_.push_back({ContextName(binding.context), ChordOf(binding)});
    }
    presentation.RefreshWarnings();
}

std::optional<input::InputAction> InputActionForRow(int row) {
    if (row < 0 || static_cast<std::size_t>(row) >= std::size(input::kRebindableActions)) return std::nullopt;
    return input::kRebindableActions[static_cast<std::size_t>(row)].action;
}

} // namespace rs::app

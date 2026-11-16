#include "InputBindingAction.h"

#include "InputSettingsRows.h"
#include "../input/InputSource.h"

namespace rs::app {

InputBindingAction::InputBindingAction(UserSettingsData& settings, input::InputSource* source)
    : settings_(settings), source_(source) {}

std::vector<input::InputBinding> InputBindingAction::Bindings() const {
    return input::BuildInputBindings(settings_.keyBindings);
}

bool InputBindingAction::Assign(int row, const std::string& bindingText) {
    return AssignUnassigning(row, bindingText, {});
}

bool InputBindingAction::AssignUnassigning(int row, const std::string& bindingText,
                                           const std::vector<int>& unassignRows) {
    const std::optional<input::InputAction> action = InputActionForRow(row);
    const std::optional<input::KeyChord> chord = input::ParseKeyChord(bindingText);
    if (!action.has_value() || !chord.has_value()) return false;
    std::vector<input::InputBinding> bindings = Bindings();
    for (const int other : unassignRows) {
        if (const std::optional<input::InputAction> otherAction = InputActionForRow(other)) {
            bindings = input::ReassignBinding(bindings, *otherAction, {input::InputKey::None, {}});
        }
    }
    Apply(input::ReassignBinding(bindings, *action, *chord));
    return true;
}

void InputBindingAction::Reset(int row) {
    const std::optional<input::InputAction> action = InputActionForRow(row);
    if (!action.has_value()) return;
    Apply(input::ResetBinding(Bindings(), *action));
}

void InputBindingAction::ResetAll() {
    Apply(input::DefaultInputBindings());
}

void InputBindingAction::Apply(const std::vector<input::InputBinding>& bindings) {
    settings_.keyBindings = input::SerializeInputOverrides(bindings);
    // 差し替える表は保存した文字列から組み直したもの(次の起動で読み込むものと同じ)。
    if (source_ != nullptr) source_->SetBindings(Bindings());
    SaveUserSettingsOrWarn(settings_, "Failed to save user settings after key binding change.");
}

} // namespace rs::app

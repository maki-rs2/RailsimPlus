#include "InputBindings.h"

#include <string_view>

#include <magic_enum/magic_enum.hpp>

#include "../workspace/CatalogCheck.h"

namespace rs::app::input {

namespace {
constexpr PointerBinding kPointerBindings[] = {
    {PointerAction::Select, PointerButton::Left, {}},
    {PointerAction::Cancel, PointerButton::Right, {}},
    {PointerAction::CameraRotate, PointerButton::Right, {}},
    {PointerAction::CameraHeight, PointerButton::Left, {false, true, false}},
    {PointerAction::CameraFov, std::nullopt, {false, true, false}},
    {PointerAction::EditorHeight, std::nullopt, {true, false, false}},
    {PointerAction::CameraDash, std::nullopt, {true, false, false}},
    {PointerAction::AngleSnap, std::nullopt, {false, true, false}},
    {PointerAction::EdgeSplit, std::nullopt, {false, false, true}},
    {PointerAction::SelectionExtend, std::nullopt, {false, true, false}},
    {PointerAction::PlacementReverse, std::nullopt, {false, true, false}},
    {PointerAction::TransformSnap, std::nullopt, {true, false, false}},
    {PointerAction::CameraPan, PointerButton::Middle, {}},
};
static_assert(std::size(kPointerBindings) == magic_enum::enum_count<PointerAction>());
static_assert(workspace::IsCatalogInEnumOrder(kPointerBindings, &PointerBinding::action));
} // namespace

const PointerBinding& GetPointerBinding(PointerAction action) {
    return kPointerBindings[workspace::EnumIndex(action)];
}

bool MatchesPointerBinding(PointerAction action, KeyModifiers modifiers) {
    const KeyModifiers required = GetPointerBinding(action).modifiers;
    // ドラッグ開始とホイールは、追加の修飾キーがあっても同じ操作として受け付ける。
    return (!required.ctrl || modifiers.ctrl) && (!required.shift || modifiers.shift) && (!required.alt || modifiers.alt);
}

std::vector<InputBinding> DefaultInputBindings() {
    return {
        {InputAction::EditorUndo, InputContext::Edit, InputKey::Z, true},
        {InputAction::EditorRedo, InputContext::Edit, InputKey::Y, true},
        {InputAction::EditorDelete, InputContext::Edit, InputKey::Delete},
        {InputAction::EditorCancel, InputContext::Edit, InputKey::Escape},
        {InputAction::CameraMoveForward, InputContext::View, InputKey::W, false, false, false, true},
        {InputAction::CameraMoveBack, InputContext::View, InputKey::S, false, false, false, true},
        {InputAction::CameraMoveLeft, InputContext::View, InputKey::A, false, false, false, true},
        {InputAction::CameraMoveRight, InputContext::View, InputKey::D, false, false, false, true},
        // Ctrl押下中のダッシュ用。移動の判定は修飾キーの完全一致なので、Ctrl付きも同じActionへ割り当てる
        // (倍率はCameraController::UpdateWasdがCtrlの状態から掛ける)。
        {InputAction::CameraMoveForward, InputContext::View, InputKey::W, true, false, false, true},
        {InputAction::CameraMoveBack, InputContext::View, InputKey::S, true, false, false, true},
        {InputAction::CameraMoveLeft, InputContext::View, InputKey::A, true, false, false, true},
        {InputAction::CameraMoveRight, InputContext::View, InputKey::D, true, false, false, true},
        {InputAction::ViewScenePrev, InputContext::View, InputKey::UpArrow, true},
        {InputAction::ViewSceneNext, InputContext::View, InputKey::DownArrow, true},
        // 修飾キーなしの↑↓。Ctrl+↑↓(Scene切替)とは修飾キーの完全一致判定で衝突しない。
        {InputAction::EditorHeightUp, InputContext::Edit, InputKey::UpArrow, false, false, false, true},
        {InputAction::EditorHeightDown, InputContext::Edit, InputKey::DownArrow, false, false, false, true},
        // 情景配置プレビュー中の向き回転。呼び出し側がsceneryPlacementMode中だけHeld()を見る。
        {InputAction::SceneryYawLeft, InputContext::Edit, InputKey::LeftArrow, false, false, false, true},
        {InputAction::SceneryYawRight, InputContext::Edit, InputKey::RightArrow, false, false, false, true},
        {InputAction::EditorSnapDisable, InputContext::Edit, InputKey::Space, false, false, false, true},
        // 運転台視点のCabinPos移動は、運転の文脈が有効な間だけ受け付ける。
        {InputAction::CabinMoveForward, InputContext::Drive, InputKey::W, false, false, false, true},
        {InputAction::CabinMoveBack, InputContext::Drive, InputKey::S, false, false, false, true},
        {InputAction::CabinMoveLeft, InputContext::Drive, InputKey::A, false, false, false, true},
        {InputAction::CabinMoveRight, InputContext::Drive, InputKey::D, false, false, false, true},
        {InputAction::CabinMoveUp, InputContext::Drive, InputKey::UpArrow, false, false, false, true},
        {InputAction::CabinMoveDown, InputContext::Drive, InputKey::DownArrow, false, false, false, true},
        // UIが先に消費したEscはフレームへ届かないため、残ったEscはUI捕捉で遮断しない。
        {InputAction::WorkspaceBack, InputContext::Ui, InputKey::Escape},
        // 編集の可否とは別にモードを切り替えられるが、入力欄がキーボードを取る間は発火させない。
        {InputAction::SelectModeRail, InputContext::View, InputKey::F1},
        {InputAction::SelectModeTrain, InputContext::View, InputKey::F2},
        {InputAction::SelectModeSignal, InputContext::View, InputKey::F3},
        {InputAction::SelectModeDia, InputContext::View, InputKey::F4},
        {InputAction::SelectModeScenery, InputContext::View, InputKey::F5},
        // どのモードでも・ImGui/RmlUiが入力を取っていても撮れるようUiコンテキストにする。
        {InputAction::TakeScreenshot, InputContext::Ui, InputKey::F12},
        {InputAction::ToggleUi, InputContext::Ui, InputKey::F11},
        {InputAction::CameraRollLeft, InputContext::View, InputKey::Q, false, false, false, true},
        {InputAction::CameraRollRight, InputContext::View, InputKey::E, false, false, false, true},
        {InputAction::CameraRollReset, InputContext::View, InputKey::R},
    };
}

std::optional<InputKey> FirstInputKeyForAction(const std::vector<InputBinding>& bindings, InputAction action) {
    for (const InputBinding& binding : bindings) {
        if (binding.action == action) return binding.key;
    }
    return std::nullopt;
}

std::string InputKeyToString(InputKey key) { return std::string(magic_enum::enum_name(key)); }

std::optional<InputKey> InputKeyFromString(const std::string& text) {
    const std::optional<InputKey> key = magic_enum::enum_cast<InputKey>(text);
    if (key == InputKey::Count) return std::nullopt; // 番兵。Noneは未割当として返す
    return key;
}

std::string FormatKeyChord(const KeyChord& chord) {
    std::string text;
    if (chord.modifiers.ctrl) text += "Ctrl+";
    if (chord.modifiers.shift) text += "Shift+";
    if (chord.modifiers.alt) text += "Alt+";
    return text + InputKeyToString(chord.key);
}

std::optional<KeyChord> ParseKeyChord(const std::string& text) {
    KeyChord chord{InputKey::Count, {}};
    std::string_view rest = text;
    while (true) {
        const std::size_t plus = rest.find('+');
        if (plus == std::string_view::npos) break;
        const std::string_view modifier = rest.substr(0, plus);
        if (modifier == "Ctrl") chord.modifiers.ctrl = true;
        else if (modifier == "Shift") chord.modifiers.shift = true;
        else if (modifier == "Alt") chord.modifiers.alt = true;
        else return std::nullopt;
        rest.remove_prefix(plus + 1);
    }
    const std::optional<InputKey> key = InputKeyFromString(std::string(rest));
    if (!key.has_value()) return std::nullopt;
    chord.key = *key;
    return chord;
}

std::string SerializeInputBindings(const std::vector<InputBinding>& bindings) {
    const auto flag = [](bool value) -> const char* { return value ? "1" : "0"; };
    std::string result;
    for (const InputBinding& binding : bindings) {
        if (!result.empty()) result += '\n';
        result += std::string(magic_enum::enum_name(binding.action)) + "," +
                  std::string(magic_enum::enum_name(binding.context)) + "," +
                  InputKeyToString(binding.key) + "," + flag(binding.ctrl) + "," +
                  flag(binding.shift) + "," + flag(binding.alt) + "," + flag(binding.held);
    }
    return result;
}

namespace {

std::vector<std::string_view> SplitView(std::string_view text, char separator) {
    std::vector<std::string_view> parts;
    while (true) {
        const std::size_t position = text.find(separator);
        if (position == std::string_view::npos) break;
        parts.push_back(text.substr(0, position));
        text.remove_prefix(position + 1);
    }
    parts.push_back(text);
    return parts;
}

std::optional<bool> ParseFlag(std::string_view text) {
    if (text == "1") return true;
    if (text == "0") return false;
    return std::nullopt;
}

std::optional<InputBinding> ParseInputBindingLine(std::string_view line) {
    const std::vector<std::string_view> fields = SplitView(line, ',');
    constexpr std::size_t kFieldCount = 7;
    if (fields.size() != kFieldCount) return std::nullopt;
    const std::optional<InputAction> action = magic_enum::enum_cast<InputAction>(fields[0]);
    const std::optional<InputContext> context = magic_enum::enum_cast<InputContext>(fields[1]);
    const std::optional<InputKey> key = InputKeyFromString(std::string(fields[2]));
    const std::optional<bool> ctrl = ParseFlag(fields[3]);
    const std::optional<bool> shift = ParseFlag(fields[4]);
    const std::optional<bool> alt = ParseFlag(fields[5]);
    const std::optional<bool> held = ParseFlag(fields[6]);
    if (!action || !context || !key || !ctrl || !shift || !alt || !held) return std::nullopt;
    return InputBinding{*action, *context, *key, *ctrl, *shift, *alt, *held};
}

} // namespace

std::vector<InputBinding> ParseInputBindings(const std::string& text) {
    std::vector<InputBinding> bindings;
    for (std::string_view line : SplitView(text, '\n')) {
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (const std::optional<InputBinding> binding = ParseInputBindingLine(line)) bindings.push_back(*binding);
    }
    return bindings;
}

} // namespace rs::app::input

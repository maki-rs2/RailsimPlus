//
// InputBindings.h - Action IDとキー・ポインタ割当の対応表
//
// 既定の割当はコード内に持つ。ユーザーの上書きは、割当列を文字列へ落とす・戻す
// 継ぎ目(SerializeInputBindings/ParseInputBindings)を通して設定に保存する
// (上書きの重ね方は InputBindingOverrides.h)。
// enumと文字列の相互変換はmagic_enumを使い、独自の文字列レジストリを作らない。
//
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "InputTypes.h"

namespace rs::app::input {

// heldは「押下継続で有効」(カメラ移動)か「押下エッジのみ」(コマンド)かの区別。
struct InputBinding {
    InputAction action;
    InputContext context;
    InputKey key;
    bool ctrl = false;
    bool shift = false;
    bool alt = false;
    bool held = false;
    bool operator==(const InputBinding&) const = default;
};

std::vector<InputBinding> DefaultInputBindings();

enum class PointerAction {
    Select, Cancel, CameraRotate, CameraHeight, CameraFov, EditorHeight, CameraDash,
    AngleSnap, EdgeSplit, SelectionExtend, PlacementReverse, TransformSnap,
};

struct PointerBinding {
    PointerAction action;
    std::optional<PointerButton> button;
    KeyModifiers modifiers;
};

const PointerBinding& GetPointerBinding(PointerAction action);
bool MatchesPointerBinding(PointerAction action, KeyModifiers modifiers);
std::optional<InputKey> FirstInputKeyForAction(const std::vector<InputBinding>& bindings, InputAction action);
std::string InputKeyToString(InputKey key);
// 列挙にある名前だけを返す(Countは番兵なので返さない)。
std::optional<InputKey> InputKeyFromString(const std::string& text);

// 主キー+修飾の1組。画面の表記は「Ctrl+Shift+Alt+キー名」(修飾はこの順)。
struct KeyChord {
    InputKey key;
    KeyModifiers modifiers;
};
std::string FormatKeyChord(const KeyChord& chord);
std::optional<KeyChord> ParseKeyChord(const std::string& text);

// 割当列を1行1バインドのテキストへ落とす(action,context,key,ctrl,shift,alt,held)。
std::string SerializeInputBindings(const std::vector<InputBinding>& bindings);

// SerializeInputBindingsの逆。壊れた行(項目数・フラグ・未知のaction/context/key名)は、その行だけ捨てる。
std::vector<InputBinding> ParseInputBindings(const std::string& text);

} // namespace rs::app::input

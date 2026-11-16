//
// InputBindingOverrides.h - 既定の割当へユーザーの再割当を重ねる処理
//
// 設定画面から変えられる操作(kRebindableActions)だけが上書きの対象で、それ以外は常に既定のまま。
// 割当表の組み立ては BuildInputBindings の1か所で、起動時も再割当・リセットの後も同じ関数を使う。
// 保存の形は SerializeInputBindings と同じテキストで、既定と違う操作の行だけを持つ。
//
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "InputBindings.h"

namespace rs::app::input {

struct RebindableAction {
    InputAction action;
    // 移動のCtrlダッシュ用の行(同じActionでCtrl付き)を、主キーに合わせて持つか。
    bool ctrlDashFollows;
};

// 設定画面に出す順。
inline constexpr RebindableAction kRebindableActions[] = {
    {InputAction::CameraMoveForward, true}, {InputAction::CameraMoveBack, true},
    {InputAction::CameraMoveLeft, true},    {InputAction::CameraMoveRight, true},
    {InputAction::EditorUndo, false},       {InputAction::EditorRedo, false},
    {InputAction::EditorDelete, false},
    {InputAction::CameraRollLeft, false}, {InputAction::CameraRollRight, false},
    {InputAction::CameraRollReset, false},
};

const RebindableAction* FindRebindableAction(InputAction action);

// 既定に、保存済みのテキストの上書きを重ねた割当表。操作ごとに、有効な行が1つでもあればその先頭の行の
// キーと修飾を主キーにし(contextとheldは既定のまま)、無ければ既定にする。
std::vector<InputBinding> BuildInputBindings(const std::string& overridesText);

// 既定と違う変更可能な操作の行だけをテキストにする。すべて既定と同じなら空文字列。
std::string SerializeInputOverrides(const std::vector<InputBinding>& bindings);

// actionの主キー(その操作の先頭の行)。
std::optional<InputBinding> FindPrimaryBinding(const std::vector<InputBinding>& bindings, InputAction action);

// actionを変更可能な操作として、主キーをchordにそろえた割当表を返す(Ctrlダッシュ行も追従させる)。
// chord.keyがNoneなら未割当にする(ダッシュ行も作らない)。
// 変更可能でない操作はそのまま返す。
std::vector<InputBinding> ReassignBinding(const std::vector<InputBinding>& bindings, InputAction action,
                                          const KeyChord& chord);

// actionの行を既定へ戻した割当表を返す。
std::vector<InputBinding> ResetBinding(const std::vector<InputBinding>& bindings, InputAction action);

} // namespace rs::app::input

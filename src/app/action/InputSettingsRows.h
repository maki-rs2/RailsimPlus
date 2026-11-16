//
// InputSettingsRows.h - ゲーム設定の操作タブの行を、実際のキー割当から作る
//
// 行に出すのは input::kRebindableActions の操作だけで、同じ順に並ぶ(行の番号 = kRebindableActionsの添字)。
// 出さない割り当て(F1〜F5・Esc・F11・F12など)は、重なりの判定用に fixed_ へ入れる。
//
#pragma once

#include <optional>
#include <vector>

#include "../input/InputBindingOverrides.h"
#include "../locale/Localization.h"
#include "../workspace/InputSettingsPresentation.h"

namespace rs::app {

// presentationのrows_とfixed_を、bindingsから作り直して警告も更新する(文言はlocalizationで翻訳する)。
void FillInputSettingsRows(workspace::InputSettingsPresentation& presentation,
                           const std::vector<input::InputBinding>& bindings, const Localization& localization);

// 行の番号に対応する操作。範囲外ならnullopt。
std::optional<input::InputAction> InputActionForRow(int row);

} // namespace rs::app

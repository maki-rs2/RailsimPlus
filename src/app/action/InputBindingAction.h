//
// InputBindingAction.h - ゲーム設定の操作タブからのキー再割当・リセット
//
// 割当表の元は設定の keyBindings(文字列)で、割当表はそこから BuildInputBindings で組み立てる。
// 変えたあとは、設定を保存し、動いている InputSource の割当表を差し替える。
//
#pragma once

#include <string>
#include <vector>

#include "../input/InputBindingOverrides.h"
#include "../settings/UserSettings.h"

namespace rs::app {

namespace input {
class InputSource;
}

class InputBindingAction {
public:
    // sourceは差し替え先(nullptrなら設定の保存だけ行う)。
    InputBindingAction(UserSettingsData& settings, input::InputSource* source);

    // 今の設定から組み立てた割当表。
    std::vector<input::InputBinding> Bindings() const;
    // 行(InputActionForRow)の主キーをbindingText(「Ctrl+Z」の形)へ変える。行か表記が読めなければfalseで何もしない。
    bool Assign(int row, const std::string& bindingText);
    // Assignの前に、unassignRows(行の番号)の操作を未割当にする。競合した行を外して割り当てるときに使う。
    // 設定の保存と入力への反映は1回にまとめる。読めないときはfalseで何もしない。
    bool AssignUnassigning(int row, const std::string& bindingText, const std::vector<int>& unassignRows);
    // 行を既定へ戻す。行が範囲外なら何もしない。
    void Reset(int row);
    void ResetAll();

private:
    void Apply(const std::vector<input::InputBinding>& bindings);

    UserSettingsData& settings_;
    input::InputSource* source_;
};

} // namespace rs::app

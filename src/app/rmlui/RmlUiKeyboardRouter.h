//
// RmlUiKeyboardRouter.h - Win32のキーボードメッセージ(WM_KEYDOWN/WM_KEYUP/WM_CHAR)の振り分け
//
// 確認ダイアログとコンテキストメニューが開いている間の上下・Enter・Esc、編成一覧の上下キー、
// テキスト入力欄にフォーカスがあるときのキー・文字入力のRmlUiへの転送を受け持つ。
// 戻り値のtrueは「消費した」(呼び出し側は他の入力へ渡さない)。
//
#pragma once

#include <windows.h>

#include <functional>

#include "../input/EscapeRouting.h"

namespace Rml {
class Context;
} // namespace Rml

namespace rs::app::rmlui {

class RmlUiConfirmDialog;
class RmlUiContextMenu;
enum class ContextMenuKey;

// いま押されている修飾キー(Shift・Ctrl・Alt)とロック状態をRml::Input::KeyModifierのビットで返す。
int KeyModifierState();

// フォーカス中の要素がテキスト入力(input[type=text系]・textarea)かどうか。contextがnullptrならfalse。
bool HasTextInputFocus(Rml::Context* context);

class RmlUiKeyboardRouter {
public:
    // 確認ダイアログ・メニューが取らなかった一覧まわりのキー(Escでパネルを閉じる・編成一覧の上下)。
    // 処理したらtrue(キーを消費する)。
    using FormationArrowHandler = std::function<bool(ContextMenuKey)>;

    // 参照はこのルーターより長く生きること(RmlUiIntegrationが持つ)。
    RmlUiKeyboardRouter(Rml::Context& context, RmlUiContextMenu& contextMenu, RmlUiConfirmDialog& confirmDialog,
                        FormationArrowHandler formationArrow);

    bool HandleKeyDown(WPARAM virtualKey, LPARAM lParam, input::EscapeHandlers escapeHandlers = {});
    bool HandleKeyUp(WPARAM virtualKey);
    bool HandleChar(WPARAM wParam);

private:
    Rml::Context& context_;
    RmlUiContextMenu& contextMenu_;
    RmlUiConfirmDialog& confirmDialog_;
    FormationArrowHandler formationArrow_;
};

} // namespace rs::app::rmlui

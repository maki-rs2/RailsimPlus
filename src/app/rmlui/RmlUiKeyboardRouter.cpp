#include "RmlUiKeyboardRouter.h"

#include <optional>
#include <string>
#include <utility>

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Input.h>

#include "../../platform/text/Encoding.h"
#include "RmlUiConfirmDialog.h"
#include "RmlUiContextMenu.h"

namespace rs::app::rmlui {

namespace {

// GetKeyStateの戻り値のビット。上位ビット=いま押されている、最下位ビット=トグル中(CapsLock等がオン)。
constexpr SHORT kKeyStateDownMask = static_cast<SHORT>(0x8000);
constexpr SHORT kKeyStateToggledMask = 1;

// WM_CHARで、これより小さい文字コードは制御文字(改行・タブ以外は入力として渡さない)。
constexpr wchar_t kFirstPrintableChar = 32;

// WM_KEYDOWNのlParamのうち、押しっぱなしの繰り返しを示すビット(直前もキーが押されていた)。
constexpr LPARAM kKeyRepeatFlag = static_cast<LPARAM>(1) << 30;

// テキスト入力欄の基本操作(カーソル移動・削除・確定)に必要な最小限のキーのみ
// 対応する(このプロトタイプのスコープ。Action層とのRmlUi統合時に拡張する)。
Rml::Input::KeyIdentifier ConvertKeyMinimal(WPARAM virtualKey) {
    switch (virtualKey) {
        case VK_BACK: return Rml::Input::KI_BACK;
        case VK_TAB: return Rml::Input::KI_TAB;
        case VK_RETURN: return Rml::Input::KI_RETURN;
        case VK_ESCAPE: return Rml::Input::KI_ESCAPE;
        case VK_DELETE: return Rml::Input::KI_DELETE;
        case VK_HOME: return Rml::Input::KI_HOME;
        case VK_END: return Rml::Input::KI_END;
        case VK_LEFT: return Rml::Input::KI_LEFT;
        case VK_RIGHT: return Rml::Input::KI_RIGHT;
        case VK_UP: return Rml::Input::KI_UP;
        case VK_DOWN: return Rml::Input::KI_DOWN;
        default: return Rml::Input::KI_UNKNOWN;
    }
}

// メニューが開いている間に取るキー。対象外のキーは無し。
std::optional<ContextMenuKey> ConvertContextMenuKey(WPARAM virtualKey) {
    switch (virtualKey) {
        case VK_UP: return ContextMenuKey::Up;
        case VK_DOWN: return ContextMenuKey::Down;
        case VK_RETURN: return ContextMenuKey::Enter;
        case VK_ESCAPE: return ContextMenuKey::Escape;
        default: return std::nullopt;
    }
}

} // namespace

int KeyModifierState() {
    int state = 0;
    if ((GetKeyState(VK_SHIFT) & kKeyStateDownMask) != 0) state |= Rml::Input::KM_SHIFT;
    if ((GetKeyState(VK_CONTROL) & kKeyStateDownMask) != 0) state |= Rml::Input::KM_CTRL;
    if ((GetKeyState(VK_MENU) & kKeyStateDownMask) != 0) state |= Rml::Input::KM_ALT;
    if ((GetKeyState(VK_CAPITAL) & kKeyStateToggledMask) != 0) state |= Rml::Input::KM_CAPSLOCK;
    if ((GetKeyState(VK_NUMLOCK) & kKeyStateToggledMask) != 0) state |= Rml::Input::KM_NUMLOCK;
    return state;
}

bool HasTextInputFocus(Rml::Context* context) {
    if (context == nullptr) return false;
    Rml::Element* const focus = context->GetFocusElement();
    if (focus == nullptr) return false;
    const Rml::String tag = focus->GetTagName();
    if (tag == "textarea") return true;
    if (tag != "input") return false;
    // 型未指定のinputはRmlUi既定でtext扱い(Source/Core/Elements/ElementFormControlInput.cpp)。
    const Rml::String type = focus->GetAttribute<Rml::String>("type", Rml::String("text"));
    return type == "text" || type == "password";
}

RmlUiKeyboardRouter::RmlUiKeyboardRouter(Rml::Context& context, RmlUiContextMenu& contextMenu,
                                         RmlUiConfirmDialog& confirmDialog, FormationArrowHandler formationArrow)
    : context_(context),
      contextMenu_(contextMenu),
      confirmDialog_(confirmDialog),
      formationArrow_(std::move(formationArrow)) {}

bool RmlUiKeyboardRouter::HandleKeyDown(WPARAM virtualKey, LPARAM lParam, input::EscapeHandlers escapeHandlers) {
    if (virtualKey == VK_ESCAPE) {
        using input::EscapeTarget;
        using workspace::EnumIndex;
        escapeHandlers[EnumIndex(EscapeTarget::ConfirmDialog)] = [&] {
            return confirmDialog_.HandleKeyDown(ContextMenuKey::Escape, (lParam & kKeyRepeatFlag) != 0);
        };
        escapeHandlers[EnumIndex(EscapeTarget::ContextMenu)] = [&] {
            return contextMenu_.HandleKeyDown(ContextMenuKey::Escape);
        };
        escapeHandlers[EnumIndex(EscapeTarget::TextInput)] = [&] {
            if (!HasTextInputFocus(&context_)) return false;
            context_.ProcessKeyDown(Rml::Input::KI_ESCAPE, KeyModifierState());
            return true;
        };
        return input::DispatchEscape(escapeHandlers);
    }
    // 上下・Enterも、開いているダイアログやメニューをテキスト入力より優先する。
    const std::optional<ContextMenuKey> menuKey = ConvertContextMenuKey(virtualKey);
    if (menuKey.has_value() && confirmDialog_.HandleKeyDown(*menuKey, (lParam & kKeyRepeatFlag) != 0)) return true;
    if (menuKey.has_value() && contextMenu_.HandleKeyDown(*menuKey)) return true;
    if (!HasTextInputFocus(&context_)) return menuKey.has_value() && formationArrow_ && formationArrow_(*menuKey);
    const Rml::Input::KeyIdentifier key = ConvertKeyMinimal(virtualKey);
    if (key != Rml::Input::KI_UNKNOWN) context_.ProcessKeyDown(key, KeyModifierState());
    return true;
}

bool RmlUiKeyboardRouter::HandleKeyUp(WPARAM virtualKey) {
    // 編成一覧の上下は押しだけを取り、離しはそのまま通す(押しをビューポートが受けていたとき、保持が残らないように)。
    const bool popupOpen = contextMenu_.IsOpen() || confirmDialog_.IsOpen();
    if (popupOpen && ConvertContextMenuKey(virtualKey).has_value()) return true;
    if (!HasTextInputFocus(&context_)) return false;
    const Rml::Input::KeyIdentifier key = ConvertKeyMinimal(virtualKey);
    if (key != Rml::Input::KI_UNKNOWN) context_.ProcessKeyUp(key, KeyModifierState());
    return true;
}

bool RmlUiKeyboardRouter::HandleChar(WPARAM wParam) {
    if (!HasTextInputFocus(&context_)) return false;
    // サロゲートペア(BMP外の文字)は扱わない(日本語の
    // 大半はBMP内のため実用上は問題ない)。
    const wchar_t codeUnit = static_cast<wchar_t>(wParam);
    if (codeUnit >= kFirstPrintableChar || codeUnit == L'\n' || codeUnit == L'\t') {
        const std::string utf8 = rs::platform_text::WideToUtf8(std::wstring(1, codeUnit));
        context_.ProcessTextInput(Rml::String(utf8.c_str(), utf8.size()));
    }
    return true;
}

} // namespace rs::app::rmlui

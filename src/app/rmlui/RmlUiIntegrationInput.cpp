// RmlUiIntegrationInput.cpp - RmlUiIntegrationのWin32入力の振り分け。
// マウス・ホイール・キー・IMEのメッセージを、UIとビューポートのどちらへ渡すか決める。
#include "RmlUiIntegration.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>

#include <windowsx.h>

#include <algorithm>
#include <cstdlib>
#include <optional>
#include <utility>

#include "RmlUiConfirmDialog.h"
#include "RmlUiContextMenu.h"
#include "RmlUiKeyboardRouter.h"
#include "RmlUiOutsideClickCloser.h"
#include "RmlUiWorkspaceView.h"

namespace rs::app::rmlui {

namespace {

// どの要素にも当たらない座標(ウィンドウの外)。
constexpr int kOffscreenMouse = -100000;
// 3D画面の押してから離すまでの移動がこのピクセル以内なら、ドラッグではなくクリックとみなす。
constexpr int kViewportClickSlopPx = 4;
// マウスボタンのメッセージ(DOWN/UP)からRmlUiのbutton_indexを求める。対象外のメッセージは値なし
// (添字に使う前に呼び出し側で確かめる)。
std::optional<int> MouseButtonIndexOf(UINT message) {
    switch (message) {
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP: return kMouseButtonLeft;
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP: return kMouseButtonRight;
        default: return std::nullopt;
    }
}

} // namespace

bool RmlUiIntegration::AnyButtonRoutedTo(ButtonRouting routing) const {
    return std::any_of(buttonRouting_.begin(), buttonRouting_.end(),
                       [routing](ButtonRouting held) { return held == routing; });
}

void RmlUiIntegration::ReleaseHeldUiButtons() {
    // 離す前にカーソルを画面の外へ動かす(ドロップ先の上で離すと落としたことになる。ProcessMouseLeaveは
    // ドロップ先を消さない)。
    if (AnyButtonRoutedTo(ButtonRouting::Ui)) {
        context_->ProcessMouseMove(kOffscreenMouse, kOffscreenMouse, KeyModifierState());
    }
    for (std::size_t i = 0; i < kMouseButtonCount; ++i) {
        if (buttonRouting_[i] != ButtonRouting::Ui) continue;
        buttonRouting_[i] = ButtonRouting::None;
        context_->ProcessMouseButtonUp(static_cast<int>(i), KeyModifierState());
    }
}

bool RmlUiIntegration::HandleWin32Message(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (context_ == nullptr || contextMenu_ == nullptr || confirmDialog_ == nullptr || keyboardRouter_ == nullptr) return false;

    if (IsImeMessage(message)) {
        // テキスト入力欄へフォーカスしているときだけIMEをRmlUiへ渡す
        if (!HasTextInputFocus()) return false;
        return !HandleImeMessage(context_, imeEditor_, hwnd, message, wParam, lParam);
    }

    switch (message) {
        case WM_MOUSEMOVE: {
            context_->ProcessMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), KeyModifierState());
            // ビューポート側で(このメッセージより前に)ボタンを押している最中は、
            // UIに当たっていても消費しない(ドラッグ中のカメラ操作を継続させるため)。
            return IsPointerOverUi() && !AnyButtonRoutedTo(ButtonRouting::Viewport);
        }
        // スクリーンショット等でフォーカスやマウス捕捉を奪われると離しが届かないので、離した扱いにする
        case WM_KILLFOCUS:
        case WM_CANCELMODE:
            if (workspaceView_ != nullptr) workspaceView_->GameSettings().InputSettings().CancelCapture();
            ReleaseHeldUiButtons();
            contextMenu_->Close();
            confirmDialog_->Close();
            return false;
        // 離し処理中のReleaseCapture()でも同期で届くので、メニューは閉じない(開いた直後に閉じてしまう)
        case WM_CAPTURECHANGED:
            ReleaseHeldUiButtons();
            return false;
        case WM_MBUTTONDOWN:
            return !IsConfirmDialogOpen() && workspaceView_ != nullptr &&
                   workspaceView_->GameSettings().InputSettings().HandleMouseDown(2);
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN: {
            const std::optional<int> buttonIndex = MouseButtonIndexOf(message);
            if (!buttonIndex.has_value()) return false;
            if (!IsConfirmDialogOpen() && workspaceView_ != nullptr &&
                workspaceView_->GameSettings().InputSettings().HandleMouseDown(*buttonIndex)) {
                buttonRouting_[static_cast<size_t>(*buttonIndex)] = ButtonRouting::Consumed;
                return true;
            }
            // メニューの外の押下は、閉じるだけで消費する(ビューポートへそのまま通さない)。
            if (contextMenu_->CloseIfOutside(static_cast<float>(GET_X_LPARAM(lParam)),
                                             static_cast<float>(GET_Y_LPARAM(lParam)))) {
                buttonRouting_[static_cast<size_t>(*buttonIndex)] = ButtonRouting::Consumed;
                return true;
            }
            const bool overUi = IsPointerOverUi();
            buttonRouting_[static_cast<size_t>(*buttonIndex)] = overUi ? ButtonRouting::Ui : ButtonRouting::Viewport;
            if (!overUi) {
                viewportPressX_ = GET_X_LPARAM(lParam);
                viewportPressY_ = GET_Y_LPARAM(lParam);
                // ビューポートにはRmlUiのclickが届かないので、入力欄のblurをここで行う。
                if (Rml::Element* focus = context_->GetFocusElement()) focus->Blur();
                return false;
            }
            // ウィンドウ外で離しても離しを受け取れるようにマウスを捕捉する
            // (受け取れないとRmlUi側の押下状態(:active)が残る)。
            SetCapture(hwnd);
            context_->ProcessMouseButtonDown(*buttonIndex, KeyModifierState());
            return true;
        }
        case WM_LBUTTONUP:
        case WM_RBUTTONUP: {
            const std::optional<int> buttonIndex = MouseButtonIndexOf(message);
            if (!buttonIndex.has_value()) return false;
            // 押下時に決めた送り先を離しまで保持する(UI上で押してビューポートで
            // 離す、またはその逆で:activeやSetCaptureが食い違わないように)。
            const ButtonRouting routing = buttonRouting_[static_cast<size_t>(*buttonIndex)];
            buttonRouting_[static_cast<size_t>(*buttonIndex)] = ButtonRouting::None;
            if (routing == ButtonRouting::Consumed) return true;
            if (routing == ButtonRouting::Viewport) {
                // 押してから離すまでほとんど動かなかった左ボタンは、画面へのクリックとして画面の状態に渡す
                // (受け取っても、ビューポート側の離しの処理は止めない)。
                const int x = GET_X_LPARAM(lParam);
                const int y = GET_Y_LPARAM(lParam);
                if (*buttonIndex == 0 && workspaceView_ != nullptr && std::abs(x - viewportPressX_) <= kViewportClickSlopPx &&
                    std::abs(y - viewportPressY_) <= kViewportClickSlopPx) {
                    workspaceView_->HandleViewportClick(static_cast<float>(x), static_cast<float>(y));
                }
                return false;
            }
            if (routing != ButtonRouting::Ui) return false;
            context_->ProcessMouseButtonUp(*buttonIndex, KeyModifierState());
            // 他のボタンが押されたまま(UI・ビューポートのどちらでも)なら捕捉を続ける
            // (ビューポート側のドラッグはMainWindow::HandleMessageが捕捉している)。
            const bool anyButtonHeld = AnyButtonRoutedTo(ButtonRouting::Ui) || AnyButtonRoutedTo(ButtonRouting::Viewport);
            if (!anyButtonHeld) ReleaseCapture();
            return true;
        }
        case WM_MOUSEWHEEL:
            contextMenu_->Close(); // スクロールで閉じる(ホイール自体は通常どおり扱う)
            {
                // 一覧のスクロールやカードの出し入れでカーソルの下の要素が入れ替わると、次のマウス移動までは
                // 当たり判定が古い(何もない場所扱いになる)ので、ホイールがカメラへ漏れる。
                // WM_MOUSEWHEELの座標は画面座標なので、クライアント座標へ直して当たり判定を取り直す。
                POINT cursor{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                if (ScreenToClient(hwnd, &cursor)) {
                    context_->ProcessMouseMove(cursor.x, cursor.y, KeyModifierState());
                }
            }
            if (!IsPointerOverUi()) return false;
            context_->ProcessMouseWheel(-static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA,
                                        KeyModifierState());
            return true;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if (message == WM_KEYDOWN && wParam == VK_ESCAPE) {
                input::EscapeHandlers handlers{};
                if (workspaceView_ != nullptr) {
                    workspaceView_->BindEscapeHandlers(handlers);
                    handlers[workspace::EnumIndex(input::EscapeTarget::InputCapture)] = [&] {
                        return !IsConfirmDialogOpen() && workspaceView_->GameSettings().InputSettings().HandleKeyDown(
                            static_cast<unsigned int>(wParam), (lParam & (static_cast<LPARAM>(1) << 30)) != 0);
                    };
                }
                return keyboardRouter_->HandleKeyDown(wParam, lParam, std::move(handlers));
            }
            if (!IsConfirmDialogOpen() && workspaceView_ != nullptr &&
                workspaceView_->GameSettings().InputSettings().HandleKeyDown(
                    static_cast<unsigned int>(wParam), (lParam & (static_cast<LPARAM>(1) << 30)) != 0)) return true;
            if (message == WM_SYSKEYDOWN) return false;
            if (!HasTextInputFocus() && !IsContextMenuOpen() && !IsConfirmDialogOpen() && workspaceView_ != nullptr &&
                workspaceView_->HandleWorkspaceKeyDown(static_cast<unsigned int>(wParam), *context_, IsModalOverlayVisible())) return true;
            return keyboardRouter_->HandleKeyDown(wParam, lParam);
        case WM_KEYUP: return keyboardRouter_->HandleKeyUp(wParam);
        case WM_CHAR: return keyboardRouter_->HandleChar(wParam);
        default:
            return false;
    }
}

} // namespace rs::app::rmlui

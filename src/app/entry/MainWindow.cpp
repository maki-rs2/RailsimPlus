//
// MainWindow.cpp - MainWindow の実装(main.cppから分離)
//
#include "MainWindow.h"

#include <windowsx.h>
#include <dwmapi.h>

#include <spdlog/spdlog.h>

#include "imgui_impl_win32.h"

#include "AppIconResource.h"
#include "../gpu/App.h"
#include "../rmlui/RmlUiIntegration.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace rs::app {

namespace {
// ウィンドウのサイズ変更/移動中に1フレーム分の処理を進めさせるためのタイマー
// (WM_ENTERSIZEMOVE〜WM_EXITSIZEMOVEの間だけ有効)。16ms=約60fps相当。
constexpr UINT_PTR kSizeMoveTimerId = 1;
constexpr UINT kSizeMoveTimerMs = 16;
// 最小クライアント領域。
// フラグに関係なく常に効かせる。
constexpr LONG kMinClientAreaSizePx = 300;

// 古いSDKのヘッダに無い場合の値(Windows 10 20H1以降で有効。それ未満では呼び出しが失敗するだけ)。
constexpr DWORD kDwmUseImmersiveDarkMode = 20;

// アプリのライト/ダーク設定。値が読めないときはライト扱い。
bool AppsUseDarkTheme() {
    DWORD lightTheme = 1;
    DWORD size = sizeof(lightTheme);
    const LSTATUS status =
        RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &lightTheme, &size);
    return status == ERROR_SUCCESS && lightTheme == 0;
}
} // namespace

void MainWindow::ApplyTitleBarTheme() {
    const BOOL dark = AppsUseDarkTheme() ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd_, kDwmUseImmersiveDarkMode, &dark, sizeof(dark));
}

MainWindow* MainWindow::s_instance = nullptr;

bool MainWindow::Create(CameraController& camera, bool viewerMode, UINT width, UINT height) {
    camera_ = &camera;
    width_ = width;
    height_ = height;
    s_instance = this;

    ImGui_ImplWin32_EnableDpiAwareness();

    wc_ = WNDCLASSEXW{sizeof(wc_)};
    wc_.style = CS_HREDRAW | CS_VREDRAW;
    wc_.lpfnWndProc = WndProcThunk;
    wc_.hInstance = GetModuleHandle(nullptr);
    wc_.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc_.hIcon = LoadIconW(wc_.hInstance, MAKEINTRESOURCEW(IDI_APP_ICON));
    wc_.hIconSm = wc_.hIcon;
    wc_.lpszClassName = L"RailsimPlusWnd";
    RegisterClassExW(&wc_);

    RECT wr = {0, 0, LONG(width), LONG(height)};
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
    hwnd_ = CreateWindowExW(0, wc_.lpszClassName,
                            viewerMode ? L"Railsim Plus プラグインビューワー"
                                       : L"Railsim Plus",
                            WS_OVERLAPPEDWINDOW, 100, 100, wr.right - wr.left,
                            wr.bottom - wr.top, nullptr, nullptr, wc_.hInstance, nullptr);
    if (!hwnd_) {
        spdlog::error("CreateWindow failed");
        return false;
    }
    ApplyTitleBarTheme();
    return true;
}

void MainWindow::SetFullscreen(bool enabled) {
    if (hwnd_ == nullptr || fullscreen_ == enabled) return;
    if (enabled) {
        MONITORINFO monitor{sizeof(MONITORINFO)};
        if (!GetMonitorInfo(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &monitor)) return;
        windowedStyle_ = GetWindowLongPtr(hwnd_, GWL_STYLE);
        GetWindowPlacement(hwnd_, &windowedPlacement_);
        SetWindowLongPtr(hwnd_, GWL_STYLE, windowedStyle_ & ~static_cast<LONG_PTR>(WS_OVERLAPPEDWINDOW));
        const RECT& area = monitor.rcMonitor;
        SetWindowPos(hwnd_, HWND_TOP, area.left, area.top, area.right - area.left, area.bottom - area.top,
                     SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    } else {
        SetWindowLongPtr(hwnd_, GWL_STYLE, windowedStyle_);
        SetWindowPlacement(hwnd_, &windowedPlacement_);
        SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }
    fullscreen_ = enabled;
}

bool MainWindow::PumpMessages() {
    MSG msg;
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
        if (msg.message == WM_QUIT) return false;
    }
    return true;
}

void MainWindow::Destroy() {
    DestroyWindow(hwnd_);
    UnregisterClassW(wc_.lpszClassName, wc_.hInstance);
}

LRESULT CALLBACK MainWindow::WndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    // --rmlui-proto/--rmlui-workspace有効時。RmlUiIntegration::HandleWin32Message
    // が「UIに当たっているか」等でメッセージごとに消費するかそのまま通すかを判定する
    // (常時全消費ではない。--rmlui-workspaceでは背後の3Dビューがマウス/キーボードを
    // 通常どおり操作できる必要があるため)。消費したメッセージには0を返す
    // (上流RmlUi_Backend_Win32と同じ)。WM_IME_REQUESTに非0を返すと、IMEは未記入の
    // IMECHARPOSITIONを有効とみなし、変換候補の位置がずれる。
    if (s_instance && s_instance->rmlUi_ != nullptr &&
        s_instance->rmlUi_->HandleWin32Message(hwnd, msg, wp, lp))
        return 0;
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return TRUE;
    if (s_instance) return s_instance->HandleMessage(hwnd, msg, wp, lp);
    return DefWindowProc(hwnd, msg, wp, lp);
}

LRESULT MainWindow::HandleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    using input::PointerAction;
    using input::PointerButton;
    const bool uiCapturesPointer = inputSource_ != nullptr && !inputSource_->AllowsPointer(input::InputContext::View);

    switch (msg) {
    case WM_GETMINMAXINFO: {
        // クライアント領域300×300を、現在のウィンドウスタイルで枠込みへ
        // 換算する(タイトルバー・枠の分だけウィンドウ全体のサイズは大きくなる)。
        RECT clientRect = {0, 0, kMinClientAreaSizePx, kMinClientAreaSizePx};
        const DWORD style = static_cast<DWORD>(GetWindowLongPtr(hwnd, GWL_STYLE));
        const DWORD exStyle = static_cast<DWORD>(GetWindowLongPtr(hwnd, GWL_EXSTYLE));
        AdjustWindowRectEx(&clientRect, style, FALSE, exStyle);
        MINMAXINFO* const minMaxInfo = reinterpret_cast<MINMAXINFO*>(lp);
        minMaxInfo->ptMinTrackSize.x = clientRect.right - clientRect.left;
        minMaxInfo->ptMinTrackSize.y = clientRect.bottom - clientRect.top;
        return 0;
    }
    case WM_ENTERSIZEMOVE:
        // ここから抜けるまでDispatchMessageの中のモーダルループに入り、メインループ
        // (RunFrameLoop)が呼ばれなくなる。タイマーで代わりに1フレームずつ進める。
        inSizeMove_ = true;
        SetTimer(hwnd, kSizeMoveTimerId, kSizeMoveTimerMs, nullptr);
        return 0;
    case WM_EXITSIZEMOVE:
        KillTimer(hwnd, kSizeMoveTimerId);
        inSizeMove_ = false;
        return 0;
    case WM_TIMER:
        if (wp == kSizeMoveTimerId && inSizeMove_) RunModalFrame();
        return 0;
    case WM_SIZE:
        if (wp != SIZE_MINIMIZED) {
            width_ = LOWORD(lp);
            height_ = HIWORD(lp);
        }
        if (app_ && wp != SIZE_MINIMIZED) app_->OnResize(LOWORD(lp), HIWORD(lp));
        // サイズ変更中は変わった直後の新しいサイズで即座に1フレーム描く
        // (タイマー任せだと次のタイマーまで最大16ms待たされ、引き伸ばされた画面が見える)。
        if (inSizeMove_) RunModalFrame();
        return 0;
    case WM_SETTINGCHANGE:
        // ライト/ダークの切り替えは "ImmersiveColorSet" で通知される。
        if (lp != 0 && wcscmp(reinterpret_cast<const wchar_t*>(lp), L"ImmersiveColorSet") == 0)
            ApplyTitleBarTheme();
        break;
    case WM_SYSCOMMAND:
        // Alt単独の押し離しでシステムメニューのモードに入ると、ビューポートの入力が
        // 奪われる。Alt+クリック(edge分割)を使うため、キー起因(lParamが0)のものは捨てる。
        // Alt+Spaceのシステムメニュー(lParamにスペース)は残す。
        if ((wp & 0xFFF0) == SC_KEYMENU && lp == 0) return 0;
        break;
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN: {
        const PointerButton button = msg == WM_LBUTTONDOWN ? PointerButton::Left :
                                     msg == WM_RBUTTONDOWN ? PointerButton::Right : PointerButton::Middle;
        const input::KeyModifiers modifiers{(GetKeyState(VK_CONTROL) & 0x8000) != 0,
                                             (GetKeyState(VK_SHIFT) & 0x8000) != 0,
                                             (GetKeyState(VK_MENU) & 0x8000) != 0};
        if (uiCapturesPointer) return 0;
        const bool cabinActive = cabinCamera_ != nullptr && cabinCamera_->active;
        if (button == input::GetPointerBinding(PointerAction::CameraRotate).button &&
            input::MatchesPointerBinding(PointerAction::CameraRotate, modifiers)) {
            if (cabinActive) cabinCamera_->OnRightDown(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            else camera_->OnRightDown(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            SetCapture(hwnd);
        }
        if (button == input::GetPointerBinding(PointerAction::CameraHeight).button) {
            const bool heightHeld = input::MatchesPointerBinding(PointerAction::CameraHeight, modifiers);
            const bool started = cabinActive
                ? cabinCamera_->OnLeftDown(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), heightHeld)
                : camera_->OnLeftDown(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), heightHeld);
            if (started) SetCapture(hwnd);
        }
        return 0;
    }
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP: {
        const PointerButton button = msg == WM_LBUTTONUP ? PointerButton::Left :
                                     msg == WM_RBUTTONUP ? PointerButton::Right : PointerButton::Middle;
        if (button == input::GetPointerBinding(PointerAction::CameraRotate).button) {
            camera_->OnRightUp();
            if (cabinCamera_ != nullptr) cabinCamera_->OnRightUp();
            ReleaseCapture();
        }
        if (button == input::GetPointerBinding(PointerAction::CameraHeight).button) {
            if (cabinCamera_ != nullptr && cabinCamera_->active) {
                if (cabinCamera_->HeightDragging()) ReleaseCapture();
                cabinCamera_->OnLeftUp();
            } else if (camera_->OnLeftUp()) {
                ReleaseCapture();
            }
        }
        return 0;
    }
    case WM_MOUSEMOVE:
        if (cabinCamera_ != nullptr && cabinCamera_->active) {
            cabinCamera_->OnMouseMove(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        } else {
            camera_->OnMouseMove(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        }
        return 0;
    case WM_MOUSEWHEEL:
        if (!uiCapturesPointer) {
            const float wheelSteps = float(GET_WHEEL_DELTA_WPARAM(wp)) / WHEEL_DELTA;
            const input::KeyModifiers modifiers{(GET_KEYSTATE_WPARAM(wp) & MK_CONTROL) != 0,
                                                 (GET_KEYSTATE_WPARAM(wp) & MK_SHIFT) != 0, false};
            const bool shiftDown = input::MatchesPointerBinding(PointerAction::CameraFov, modifiers);
            const bool ctrlDown = input::MatchesPointerBinding(PointerAction::EditorHeight, modifiers);
            if (ctrlDown && ctrlWheelHandler_ && ctrlWheelHandler_(wheelSteps)) {
                // 高さ調整が受け取った(カメラのズームへは回さない)。
            } else {
                camera_->OnWheel(wheelSteps, shiftDown);
            }
        }
        return 0;
    case WM_CLOSE:
        // 登録があれば閉じずに要求だけ渡す(未保存の警告を出すため)。閉じるときはCloseNow。
        if (closeRequestHandler_) {
            closeRequestHandler_();
            return 0;
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

void MainWindow::SetCloseRequestHandler(std::function<void()> handler) {
    closeRequestHandler_ = std::move(handler);
}

void MainWindow::CloseNow() {
    closeRequestHandler_ = nullptr;
    PostMessageW(hwnd_, WM_CLOSE, 0, 0);
}

void MainWindow::RunModalFrame() {
    if (!modalFrameCallback_ || inModalFrame_) return;
    inModalFrame_ = true;
    modalFrameCallback_();
    inModalFrame_ = false;
}

} // namespace rs::app

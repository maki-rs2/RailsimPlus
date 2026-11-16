//
// MainWindow.h - Win32ウィンドウの生成・メッセージポンプ・カメラ入力の委譲
//
// アプリケーションは単一ウィンドウのみを想定するため、WndProcからのアクセス用に
// 唯一のインスタンスへの静的ポインタを内部に保持する(複数生成は不可)。
//
#pragma once

#include <functional>

#include <windows.h>

#include "../camera/CameraControl.h"
#include "../camera/CabinViewCamera.h"
#include "../input/InputSource.h"

namespace rs::app::rmlui {
class RmlUiIntegration;
}

namespace rs::app {

class App;

class MainWindow {
public:
    // ウィンドウクラスを登録し、指定サイズでウィンドウを生成する。camera への
    // 参照はマウス入力(右ドラッグ回転/Shift+左ドラッグ高さ調節/ホイール)の
    // 委譲に使う。生成失敗時は false を返す(ログはこの関数内で出す)。
    bool Create(CameraController& camera, bool viewerMode, UINT width, UINT height);
    // Create が失敗したときの GetLastError の値(成功時は 0)。
    DWORD CreateError() const { return createError_; }

    HWND Handle() const { return hwnd_; }
    UINT Width() const { return width_; }
    UINT Height() const { return height_; }

    // App::Init成功後に呼ぶ。以降 WM_SIZE で app.OnResize() を呼ぶようになる。
    void SetDeviceReady(App* app) { app_ = app; }
    void SetInputSource(const rs::app::input::InputSource* source) { inputSource_ = source; }
    // --rmlui-proto有効時のみ設定される。非nullの間、
    // WndProcThunkがマウス/キーボード/IMEメッセージをこちらへ優先的に転送する。
    void SetRmlUiIntegration(rs::app::rmlui::RmlUiIntegration* integration) { rmlUi_ = integration; }
    // 運転台視点カメラ。activeな間は
    // 右ドラッグ/マウス移動を俯瞰カメラ(camera_)の代わりにこちらへ委譲する
    // (既存の俯瞰カメラ操作を無効化する)。
    void SetCabinViewCamera(CabinViewCamera* cabinCamera) { cabinCamera_ = cabinCamera; }

    // ウィンドウのサイズ変更/移動中(Win32のモーダルループがDispatchMessageの中で
    // 回っている間)は、メインループ(RunFrameLoop)が呼ばれず描画が止まる。その間も
    // 1フレーム分の処理を進めさせるためのコールバック。RunFrameLoopがループ開始時に
    // 登録し、終了時にnullptr指定で解除する。
    void SetModalFrameCallback(std::function<void()> callback) { modalFrameCallback_ = std::move(callback); }

    // Ctrl+ホイールの受け取り先。ホイールのノッチ数を受け取り、使ったらtrueを返す
    // (falseならカメラのズームへ回す)。RunFrameLoopがループ開始時に登録し、終了時にnullptr指定で解除する。
    void SetCtrlWheelHandler(std::function<bool(float)> handler) { ctrlWheelHandler_ = std::move(handler); }
    // Shift+ホイールの受け取り先(ファインダー中の焦点距離)。ノッチ数を受け取り、使ったらtrueを返す
    // (falseなら通常画面の視野角の調整へ回す)。登録と解除は SetCtrlWheelHandler と同じ。
    void SetFovWheelHandler(std::function<bool(float)> handler) { fovWheelHandler_ = std::move(handler); }
    // WM_CLOSE(ウィンドウの×やAlt+F4)を受けたときに呼ぶ関数を登録する。登録があるとウィンドウは閉じない。
    void SetCloseRequestHandler(std::function<void()> handler);
    // 登録を外してウィンドウを閉じる。
    void CloseNow();

    // 全画面表示(枠なしでウィンドウのあるモニター全体に広げる)へ切り替える。同じ状態なら何もしない。
    // 戻すときは、全画面にする直前の位置・大きさ・枠の種類へ戻す。
    void SetFullscreen(bool enabled);
    bool IsFullscreen() const { return fullscreen_; }

    // このフレームで溜まっているWin32メッセージを処理する。WM_QUITを受け取ったら
    // false を返す(呼び出し側はメインループを終了する)。
    bool PumpMessages();

    // ウィンドウ/ウィンドウクラスを破棄する。
    void Destroy();

private:
    static LRESULT CALLBACK WndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    // SetModalFrameCallbackで登録された関数を呼ぶ(未登録/再入中は何もしない)。
    void RunModalFrame();
    // Windowsのアプリ用ライト/ダーク設定を読み、タイトルバーの色へ反映する。
    void ApplyTitleBarTheme();

    App* app_ = nullptr; // nullptrの間はOnResizeを呼ばない(デバイス未初期化)
    rs::app::rmlui::RmlUiIntegration* rmlUi_ = nullptr;
    CameraController* camera_ = nullptr;
    CabinViewCamera* cabinCamera_ = nullptr;
    const rs::app::input::InputSource* inputSource_ = nullptr;
    HWND hwnd_ = nullptr;
    DWORD createError_ = 0;
    WNDCLASSEXW wc_{};
    UINT width_ = 0, height_ = 0;

    std::function<void()> modalFrameCallback_;
    std::function<bool(float)> ctrlWheelHandler_;
    std::function<bool(float)> fovWheelHandler_;
    std::function<void()> closeRequestHandler_;
    bool inSizeMove_ = false;   // WM_ENTERSIZEMOVE〜WM_EXITSIZEMOVEの間true
    bool inModalFrame_ = false; // RunModalFrame実行中の再入防止フラグ
    bool fullscreen_ = false;
    // 全画面にする直前の枠の種類と位置(戻すとき用)。
    LONG_PTR windowedStyle_ = 0;
    WINDOWPLACEMENT windowedPlacement_{sizeof(WINDOWPLACEMENT)};

    static MainWindow* s_instance; // 単一ウィンドウ前提。WndProcThunkから参照する。
};

} // namespace rs::app

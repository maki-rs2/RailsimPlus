//
// AppInit.h - D3D12デバイス+ImGui(Win32/DX12バックエンド)の初期化・解放
//
// main.cppのフレームループ本体から、起動時1回だけの初期化/終了処理を分離する。
//
#pragma once

#include <windows.h>

#include "imgui.h"

namespace rs::app {

class App;

// App::Init(D3D12デバイス/スワップチェーン)→ImGuiコンテキスト生成→
// Win32/DX12バックエンド初期化→日本語フォントアトラス初回ビルド、までを行う。
// viewerBackground はプラグインビューアーモードの背景色にするか(App::Init へそのまま渡す)。
// 失敗時はfalseを返す(ログはこの関数内で出す)。
bool InitGraphicsAndImGui(App& app, HWND hwnd, UINT width, UINT height,
                          ImVector<ImWchar>& fontRanges, bool viewerBackground = false);

// ImGui(DX12/Win32バックエンド+コンテキスト)とAppを初期化と逆順で解放する。
void ShutdownGraphicsAndImGui(App& app);

} // namespace rs::app

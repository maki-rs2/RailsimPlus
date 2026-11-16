//
// GpuInitFailureDialog.h - グラフィックスの初期化に失敗して起動を止めるときの、エラーのメッセージボックス
//
#pragma once

#include <windows.h>

#include <string>

#include "../gpu/GpuInitFailure.h"

namespace rs::app {

class Localization;

// 理由の1行(「Shader Model 6.6 に対応していません(検出: 6.5)」など)を、表示中の言語で作る。
std::string GpuInitFailureReason(const Localization& text, const GpuInitFailure& failure);

// GPU名と理由を載せたエラーのメッセージボックスを出す(OK を押すまで戻らない)。
// ヘッドレス実行(--smoke)ではモーダルで止まらないよう、呼び出し側が出さないようにする。
void ShowGpuInitFailureDialog(HWND owner, const Localization& text, const GpuInitFailure& failure);

} // namespace rs::app

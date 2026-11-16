//
// StartupFailureDialog.h - 起動を止めて終了するときの、エラーのメッセージボックス
//
#pragma once

#include <string>

namespace rs::app {

class Localization;

// エラーのメッセージボックスを出す(OK を押すまで戻らない)。起動を止めるときの表示はすべてここを通す。
// 親ウィンドウは付けず、最前面に出す(表示前のウィンドウを親にすると、タスクバーにも出ず見えないことがある)。
void ShowFatalErrorBox(const std::string& title, const std::string& body);

// 「起動できませんでした」のダイアログを、理由の1行を載せて出す。
// ヘッドレス実行ではモーダルで止まらないよう、呼び出し側が IsUnattendedRun で出さないようにする。
void ShowStartupFailureDialog(const Localization& text, const std::string& reason);

} // namespace rs::app

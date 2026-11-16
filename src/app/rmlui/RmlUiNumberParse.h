//
// RmlUiNumberParse.h - 入力欄の文字を数として読む小さな共通関数(敷設エディタ・プラグイン設定パネルで共用)
//
#pragma once

#include <string>
#include <string_view>

namespace rs::app::rmlui {

// 前後の空白を除く。
std::string_view TrimSpaces(std::string_view text);

// 文字列全体が有限の数として読めたときだけ、valueへ入れてtrueを返す(前後の空白は無視する)。
bool ParseFloatText(const std::string& text, float& value);
bool ParseDoubleText(const std::string& text, double& value);
bool ParseIntText(const std::string& text, int& value);

} // namespace rs::app::rmlui

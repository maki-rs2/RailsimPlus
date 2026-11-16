//
// EncodingBackend.h - 文字コード変換のOS別実装(EncodingWin32.cpp / EncodingPosix.cpp)の内部境界。
//
#pragma once

#include <string>

namespace rs::platform_text {

// CP932 -> ワイド文字列。不正なバイトは U+FFFD にする。
std::wstring Cp932ToWide(const std::string& sjis);

// ワイド文字列 -> CP932。表せない文字は '?' にする。
std::string WideToCp932(const std::wstring& wide);

} // namespace rs::platform_text

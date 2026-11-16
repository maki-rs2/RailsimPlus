//
// AsciiCase.h - ASCII範囲だけを対象にした大文字小文字・空白の小さな判定(ヘッダのみ)
//
// パスやキー文字列の比較用。ロケールに依存せず、非ASCIIのバイトはそのまま扱う。
//
#pragma once

#include <algorithm>
#include <cctype>
#include <string>

namespace rs::platform_text {

inline std::string ToAsciiLower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

// ASCIIの大文字小文字を区別せずに等しいか。
inline bool EqualsIgnoreAsciiCase(const std::string& a, const std::string& b) {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](unsigned char x, unsigned char y) {
               return std::tolower(x) == std::tolower(y);
           });
}

// 空文字、または空白文字だけか。
inline bool IsAsciiBlank(const std::string& text) {
    return std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isspace(c) != 0; });
}

} // namespace rs::platform_text

//
// SearchTerms.h - 検索欄の入力を語に分けて、すべての語を含むかを調べる(ヘッダのみ)
//
// プラグインブラウザとレイアウトの一覧で同じ絞り込みにするための共通部品。ASCIIの大文字小文字は区別しない。
//
#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include "AsciiCase.h"

namespace rs::platform_text {

// 空白で区切った語(ASCII小文字化済み)。
inline std::vector<std::string> SplitSearchTerms(const std::string& search) {
    const auto isSpace = [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; };
    std::vector<std::string> terms;
    std::size_t pos = 0;
    while (pos < search.size()) {
        while (pos < search.size() && isSpace(search[pos])) ++pos;
        const std::size_t begin = pos;
        while (pos < search.size() && !isSpace(search[pos])) ++pos;
        if (pos > begin) terms.push_back(ToAsciiLower(search.substr(begin, pos - begin)));
    }
    return terms;
}

// haystackLower(ASCII小文字化済み)がtermsのすべてを含むか。termsが空なら真。
inline bool ContainsAllTerms(const std::string& haystackLower, const std::vector<std::string>& terms) {
    return std::all_of(terms.begin(), terms.end(), [&haystackLower](const std::string& term) {
        return haystackLower.find(term) != std::string::npos;
    });
}

} // namespace rs::platform_text

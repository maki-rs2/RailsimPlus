//
// RmlUiEnumKey.h - 列挙とRML側の文字列(data-ifの比較・イベント引数・バインド値)の相互変換
//
// RMLで使う文字列は「列挙の名前を全部小文字にしたもの」に決める(例: RailTab::Lay -> "lay"、
// TabContent::PluginBrowser -> "pluginbrowser")。書く向き(ToEnumKey)と読む向き(ParseEnumKey)を
// 1組のテンプレートにまとめ、列挙ごとの手書きの対応表を作らない。列挙に値を足せば、
// RML側のキー(列挙名の小文字)もそのまま増える。
//
// RmlUiには依存しない(Rml::Stringは既定でstd::stringのため、そのまま渡せる)。
//
#pragma once

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>

#include <magic_enum/magic_enum.hpp>

#include "../../platform/text/AsciiCase.h"

namespace rs::app::rmlui {

// 列挙値をRML側の文字列(列挙名の小文字)へ変換する。
template <typename E>
std::string ToEnumKey(E value) {
    const std::string_view name = magic_enum::enum_name(value);
    return rs::platform_text::ToAsciiLower(std::string(name));
}

// RML側の文字列から列挙値を引く。大文字小文字は区別しない。未知の文字列はnullopt。
template <typename E>
std::optional<E> ParseEnumKey(std::string_view key) {
    return magic_enum::enum_cast<E>(key, magic_enum::case_insensitive);
}

} // namespace rs::app::rmlui

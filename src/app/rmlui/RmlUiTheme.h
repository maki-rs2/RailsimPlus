//
// RmlUiTheme.h - assets/config/ui_theme.json(ビルド時埋め込み)からRCSSの配色スロット
// (`--*` カスタムプロパティ)を組み立て、RmlUiの文書/コンテキストへ適用するための
// 純粋関数群。
//
// 色の値の正典はJSON、キーはRCSS変数名
// から先頭`--`を除いたもの。テーマ切替は `@media (theme: <名前>)` + `Context::ActivateTheme`
// (公式サンプル
// build/_deps/rmlui-src/Samples/basic/variables/data/variables.rml と同じ構成)。
//
#pragma once

#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Rml {
class Context;
class ElementDocument;
} // namespace Rml

namespace rs::app::rmlui {

// テーマ内の配色。キーはRCSS変数名から先頭`--`を除いたもの(例 "bg-window")、
// 値は"#RRGGBB"または"#RRGGBBAA"形式の文字列。
using UiThemeColors = std::map<std::string, std::string>;

struct UiThemeSet {
    std::string baseTheme; // themesのキーのいずれか(既定テーマ名、@media無しでbodyへ直接出す)
    std::map<std::string, UiThemeColors> themes; // テーマ名 -> 配色
};

// assets/config/ui_theme.json相当のテキストをパースする。JSON構文エラー・baseTheme未指定・
// baseThemeがthemesに無い場合はspdlog::warnを出し、空のUiThemeSet(baseTheme空・themes空)を
// 返す。値が"#RRGGBB"/"#RRGGBBAA"形式でないキーはspdlog::warnを出してそのキーを捨てる。
// 各テーマのキー集合をベーステーマと比較し、欠けているキー・余分なキーをテーマ名付きで
// warnする(欠けたキーはRCSS上でベースの値に黙って落ちるため、目視で気付けない)。
UiThemeSet ParseUiThemeJson(std::string_view json);

// 埋め込みJSON(rs::embedded::kUiThemeJson、CMakeLists.txtのrs_embed_config_json参照)から
// UiThemeSetを読み込む。
UiThemeSet LoadEmbeddedUiTheme();

// 選べるテーマ名の並び。ベーステーマが先頭で、残りは名前順。
inline std::vector<std::string> UiThemeNames(const UiThemeSet& themeSet) {
    std::vector<std::string> names;
    if (themeSet.themes.contains(themeSet.baseTheme)) names.push_back(themeSet.baseTheme);
    for (const std::pair<const std::string, UiThemeColors>& entry : themeSet.themes) {
        if (entry.first != themeSet.baseTheme) names.push_back(entry.first);
    }
    return names;
}

// 要求されたテーマ名を、themesにある名前へ解決する。空・themesに無い名前はベーステーマにする。
inline std::string ResolveUiThemeName(const UiThemeSet& themeSet, const std::string& requested) {
    return themeSet.themes.contains(requested) ? requested : themeSet.baseTheme;
}

// UiThemeSetから `body { --*: ...; }` (ベーステーマ) + 各テーマの
// `@media (theme: <名前>) { body { --*: ...; } }` からなるRCSSソース文字列を生成する。
// themeSet.baseThemeが空、またはthemes内に見つからない場合は空文字列を返す。
std::string BuildUiThemeRcss(const UiThemeSet& themeSet);

// 生成したテーマRCSS(themeRcss)を`Rml::Factory::InstanceStyleSheetString`でインスタンス化し、
// documentの既存スタイルシートと結合して(テーマ→文書自身のシートの順)documentへ設定する。
// themeRcssが空、またはRCSSのパースに失敗した場合はfalseを返し、documentのスタイルシートは
// 変更しない。
bool ApplyUiThemeStyleSheet(Rml::ElementDocument& document, const std::string& themeRcss);

// themeName以外の全テーマを無効化してからthemeNameを有効化する。
// `Context::ActivateTheme`は指定テーマの有効/無効切替のみを行い他テーマを自動で無効化
// しないため、切替前に明示的に他テーマを全てfalseにする必要がある。
void ActivateUiTheme(Rml::Context& context, const UiThemeSet& themeSet, const std::string& themeName);

} // namespace rs::app::rmlui

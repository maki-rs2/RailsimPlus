#include "RmlUiTheme.h"

#include "../../core/ConfigJson.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/StyleSheetContainer.h>

#include <cctype>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "EmbeddedUiTheme.h"

namespace rs::app::rmlui {

namespace {

bool IsHexDigit(char c) {
    return std::isxdigit(static_cast<unsigned char>(c)) != 0;
}

// 色文字列の長さ(先頭の#を含む)。"#RRGGBB"と"#RRGGBBAA"。
constexpr std::size_t kHexColorRgbLength = 7;
constexpr std::size_t kHexColorRgbaLength = 9;

// "#RRGGBB"または"#RRGGBBAA"のみ許可する。
bool IsValidHexColor(const std::string& value) {
    if ((value.size() != kHexColorRgbLength && value.size() != kHexColorRgbaLength) || value.front() != '#') return false;
    for (std::size_t i = 1; i < value.size(); ++i) {
        if (!IsHexDigit(value[i])) return false;
    }
    return true;
}

// テーマ間のキー集合の食い違いをログへ出す(欠けたキーはRCSS上でベースの値へ黙って
// フォールバックするため、目視で気付けない)。
void WarnKeySetMismatch(const std::string& baseThemeName, const UiThemeColors& baseColors,
                        const std::map<std::string, UiThemeColors>& themes) {
    for (const std::pair<const std::string, UiThemeColors>& entry : themes) {
        const std::string& themeName = entry.first;
        if (themeName == baseThemeName) continue;
        const UiThemeColors& colors = entry.second;
        for (const std::pair<const std::string, std::string>& baseEntry : baseColors) {
            if (colors.find(baseEntry.first) == colors.end()) {
                spdlog::warn("[ui_theme] テーマ'{}'にキー'{}'がありません(ベーステーマの値のままになります)",
                             themeName, baseEntry.first);
            }
        }
        for (const std::pair<const std::string, std::string>& colorEntry : colors) {
            if (baseColors.find(colorEntry.first) == baseColors.end()) {
                spdlog::warn("[ui_theme] テーマ'{}'に余分なキー'{}'があります(ベーステーマに存在しません)",
                             themeName, colorEntry.first);
            }
        }
    }
}

// 配色の変数を1行ずつ`--キー: 値;`の形でrcssへ書き足す。indentは各行の字下げ。
void AppendColorVariables(std::string& rcss, const UiThemeColors& colors, std::string_view indent) {
    for (const std::pair<const std::string, std::string>& entry : colors) {
        rcss += indent;
        rcss += "--" + entry.first + ": " + entry.second + ";\n";
    }
}

} // namespace

UiThemeSet ParseUiThemeJson(std::string_view json) {
    return rs::core::ReadConfigJson<UiThemeSet>(json, "UiTheme", [](const nlohmann::json& root) {
        UiThemeSet result;
        const nlohmann::json::const_iterator baseThemeIt = root.find("baseTheme");
        if (baseThemeIt == root.end() || !baseThemeIt->is_string() || baseThemeIt->get<std::string>().empty()) {
            spdlog::warn("[ui_theme] baseThemeが指定されていません");
            return result;
        }
        const std::string baseTheme = baseThemeIt->get<std::string>();

        const nlohmann::json::const_iterator themesIt = root.find("themes");
        if (themesIt == root.end() || !themesIt->is_object()) {
            spdlog::warn("[ui_theme] themesが見つかりません、またはオブジェクトではありません");
            return result;
        }

        std::map<std::string, UiThemeColors> themes;
        for (nlohmann::json::const_iterator themeIt = themesIt->begin(); themeIt != themesIt->end(); ++themeIt) {
            const std::string& themeName = themeIt.key();
            if (!themeIt.value().is_object()) {
                spdlog::warn("[ui_theme] テーマ'{}'の値がオブジェクトではありません", themeName);
                continue;
            }
            UiThemeColors colors;
            for (nlohmann::json::const_iterator colorIt = themeIt.value().begin(); colorIt != themeIt.value().end();
                 ++colorIt) {
                if (!colorIt.value().is_string()) {
                    spdlog::warn("[ui_theme] テーマ'{}'のキー'{}'の値が文字列ではありません", themeName, colorIt.key());
                    continue;
                }
                const std::string colorValue = colorIt.value().get<std::string>();
                if (!IsValidHexColor(colorValue)) {
                    spdlog::warn("[ui_theme] テーマ'{}'のキー'{}'の値'{}'は#RRGGBB/#RRGGBBAA形式ではありません。無視します",
                                 themeName, colorIt.key(), colorValue);
                    continue;
                }
                colors.emplace(colorIt.key(), colorValue);
            }
            themes.emplace(themeName, std::move(colors));
        }

        const std::map<std::string, UiThemeColors>::const_iterator baseIt = themes.find(baseTheme);
        if (baseIt == themes.end()) {
            spdlog::warn("[ui_theme] baseTheme'{}'がthemesに存在しません", baseTheme);
            return result;
        }

        WarnKeySetMismatch(baseTheme, baseIt->second, themes);

        result.baseTheme = baseTheme;
        result.themes = std::move(themes);
        return result;
    });
}

UiThemeSet LoadEmbeddedUiTheme() {
    const char* const first = reinterpret_cast<const char*>(rs::embedded::kUiThemeJson);
    return ParseUiThemeJson(std::string_view(first, rs::embedded::kUiThemeJsonSize));
}

std::string BuildUiThemeRcss(const UiThemeSet& themeSet) {
    if (themeSet.baseTheme.empty()) return std::string();
    const std::map<std::string, UiThemeColors>::const_iterator baseIt = themeSet.themes.find(themeSet.baseTheme);
    if (baseIt == themeSet.themes.end()) return std::string();

    std::string rcss;
    rcss += "body\n{\n";
    AppendColorVariables(rcss, baseIt->second, "\t");
    rcss += "}\n";

    for (const std::pair<const std::string, UiThemeColors>& entry : themeSet.themes) {
        if (entry.first == themeSet.baseTheme) continue;
        rcss += "\n@media (theme: " + entry.first + ")\n{\n\tbody\n\t{\n";
        AppendColorVariables(rcss, entry.second, "\t\t");
        rcss += "\t}\n}\n";
    }
    return rcss;
}

bool ApplyUiThemeStyleSheet(Rml::ElementDocument& document, const std::string& themeRcss) {
    if (themeRcss.empty()) return false;

    const Rml::SharedPtr<Rml::StyleSheetContainer> themeSheet = Rml::Factory::InstanceStyleSheetString(themeRcss);
    if (!themeSheet) {
        spdlog::error("[ui_theme] テーマRCSSのパースに失敗しました");
        return false;
    }

    const Rml::StyleSheetContainer* const existing = document.GetStyleSheetContainer();
    const Rml::SharedPtr<Rml::StyleSheetContainer> combined =
        existing != nullptr ? themeSheet->CombineStyleSheetContainer(*existing) : themeSheet;
    document.SetStyleSheetContainer(combined);
    return true;
}

void ActivateUiTheme(Rml::Context& context, const UiThemeSet& themeSet, const std::string& themeName) {
    for (const std::pair<const std::string, UiThemeColors>& entry : themeSet.themes) {
        if (entry.first != themeName) context.ActivateTheme(entry.first, false);
    }
    context.ActivateTheme(themeName, true);
}

} // namespace rs::app::rmlui

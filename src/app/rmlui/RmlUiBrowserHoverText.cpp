#include "RmlUiBrowserHoverText.h"

#include <algorithm>

#include "RmlUiPluginBrowserModel.h"

namespace rs::app::rmlui {
namespace {

// 説明文の改行は空白に変える(詳細は1行で出すため)。
std::string ToSingleLine(std::string text) {
    std::replace(text.begin(), text.end(), '\r', ' ');
    std::replace(text.begin(), text.end(), '\n', ' ');
    return text;
}

constexpr const char* kAuthorSeparator = " / ";

} // namespace

BrowserHoverText BuildBrowserHoverText(const plugin_browser::PluginCatalogEntry& entry) {
    BrowserHoverText text;
    text.name = BrowserEntryDisplayName(entry);
    const std::string description = ToSingleLine(entry.description);
    if (entry.author.empty()) {
        text.detail = description;
    } else if (description.empty()) {
        text.detail = entry.author;
    } else {
        text.detail = entry.author + kAuthorSeparator + description;
    }
    return text;
}

} // namespace rs::app::rmlui

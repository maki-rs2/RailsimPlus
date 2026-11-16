//
// RmlUiBrowserHoverText.h - カードにマウスを乗せている間に出す詳細の文(名前の全文と、作者・説明文)
//
#pragma once

#include <string>

#include "../plugin_browser/PluginCatalog.h"

namespace rs::app::rmlui {

struct BrowserHoverText {
    std::string name;   // 1行目。名前の全文
    std::string detail; // 2行目。「作者 / 説明文」(どちらかが空なら片方だけ)
    bool operator==(const BrowserHoverText&) const = default;
};

BrowserHoverText BuildBrowserHoverText(const plugin_browser::PluginCatalogEntry& entry);

} // namespace rs::app::rmlui

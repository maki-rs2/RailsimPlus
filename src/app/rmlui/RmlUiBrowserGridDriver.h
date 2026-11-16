//
// RmlUiBrowserGridDriver.h - ブラウザのカードグリッドの毎フレームの測定と、窓の範囲・カードの寸法の決定
//
// グリッド(id="browser-grid")のスクロール位置・描かれたカードを
// 測り(MeasureBrowserGrid)、カードの幅を決め(RmlUiBrowserCardSizer)、行ごとの高さを覚えて
// スクロールを補正し(BrowserRowHeights)、窓の範囲を出す。データモデルへの反映は呼び出し側(RmlUiPluginBrowserView)。
// DOMへの書き込みは、グリッドのスクロール位置(先頭へ戻す・補正)だけ。
//
#pragma once

#include <vector>

#include "../plugin_browser/BrowserCardSizeCatalog.h"
#include "../plugin_browser/BrowserRowHeights.h"
#include "../plugin_browser/PluginBrowserState.h"
#include "RmlUiBrowserCardSizer.h"

namespace Rml {
class ElementDocument;
} // namespace Rml

namespace rs::app::rmlui {

struct BrowserGridFrame {
    plugin_browser::BrowserWindow window;
    plugin_browser::BrowserCardLayout layout; // いまデータモデルへ流すカードの幅とサムネイルの高さ
};

class RmlUiBrowserGridDriver {
public:
    // 毎フレーム、カードの一覧を組み立てる前に呼ぶ。itemsはグリッドの項目(PluginBrowserState::Items())、scrollToTopは
    // このフレームにグリッドを先頭へ戻す指示。
    BrowserGridFrame Update(Rml::ElementDocument& document, const std::vector<plugin_browser::EntryId>& items,
                            const plugin_browser::BrowserCardSizeTraits& traits, bool scrollToTop);

private:
    // カードの幅の決定と、測定値が今の幅のものかの判定。
    RmlUiBrowserCardSizer cardSizer_;
    // 行ごとの高さの覚え書きとスクロールの補正(窓の計算の入力)。
    plugin_browser::BrowserRowHeights rowHeights_;
};

} // namespace rs::app::rmlui

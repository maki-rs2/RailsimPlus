#include "RmlUiBrowserGridDriver.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include "RmlUiBrowserGridMeasure.h"

namespace rs::app::rmlui {
namespace {

// workspace.rmlのスクロールするグリッドのid。
constexpr const char* kGridId = "browser-grid";
// テンプレートのカードを並べるグリッドのid。ブラウザのグリッドが隠れている間は、こちらで幅を測る。
constexpr const char* kTemplateGridId = "browser-template-grid";

} // namespace

BrowserGridFrame RmlUiBrowserGridDriver::Update(Rml::ElementDocument& document,
                                                const std::vector<plugin_browser::EntryId>& items,
                                                const plugin_browser::BrowserCardSizeTraits& traits,
                                                bool scrollToTop) {
    // 項目が変わっていれば、行の高さの覚え書きを捨てる(測る前に)。
    rowHeights_.SetResults(items);
    // 先頭へ戻す指示は、測る前に反映する(戻したあとのスクロール位置で窓を出すため)。
    Rml::Element* const grid = document.GetElementById(kGridId);
    if (grid != nullptr && scrollToTop) grid->SetScrollTop(0.0f);
    const BrowserGridMetrics metrics = grid != nullptr ? MeasureBrowserGrid(*grid) : BrowserGridMetrics{};

    // カードの幅を決める。流した幅で描かれるまでの測定値(列数・行の高さ)は古いので使わない。
    const Rml::Context* const context = document.GetContext();
    const float dpRatio = context != nullptr ? context->GetDensityIndependentPixelRatio() : 1.0f;
    // ブラウザのグリッドが隠れている間(テンプレートのページ・列車の一覧のページ)は、見えているテンプレートのグリッドで幅を決める。
    // 行の高さの観測は、ブラウザのグリッドを測った値だけを使う。
    BrowserGridMetrics sizingMetrics = metrics;
    if (metrics.innerWidth <= 0.0f) {
        Rml::Element* const templateGrid = document.GetElementById(kTemplateGridId);
        if (templateGrid != nullptr) sizingMetrics = MeasureBrowserGrid(*templateGrid);
    }
    const BrowserCardSizing sizing = cardSizer_.Update(sizingMetrics, traits);

    // 測った行の高さを覚え、上の行の高さが変わった分だけスクロールをずらしてから窓を出す。
    plugin_browser::BrowserRowObservation observation;
    observation.current = sizing.metricsCurrent && metrics.columns > 0;
    observation.columns = metrics.columns;
    observation.cardWidthPx = sizing.layout.cardWidth;
    observation.size = traits.size;
    observation.drawnCardCount = metrics.drawnCardCount;
    observation.drawnRowHeights = metrics.rowHeights;
    observation.scrollTop = metrics.scrollTop;
    observation.scrollWasReset = scrollToTop && grid != nullptr;
    observation.fallbackRowHeight = plugin_browser::EstimateBrowserCardHeight(sizing.layout, traits, sizing.gapPx, dpRatio);
    float scrollTop = rowHeights_.Observe(observation);
    if (grid != nullptr && scrollTop != metrics.scrollTop) {
        grid->SetScrollTop(scrollTop);
        scrollTop = grid->GetScrollTop(); // 範囲外は丸められるので、読み直した値で窓を出す
    }

    BrowserGridFrame frame;
    frame.layout = sizing.layout;
    frame.window = rowHeights_.Plan(scrollTop, metrics.viewHeight, items.size());
    return frame;
}

} // namespace rs::app::rmlui

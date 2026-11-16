#include "RmlUiBrowserCardSizer.h"

#include <cmath>

namespace rs::app::rmlui {
namespace {

// 描かれたカードの幅と、決めた幅が「同じ」とみなせる差(px)。小数の丸めの揺れを吸収するだけの値。
constexpr float kSameWidthTolerancePx = 0.5f;

bool SameWidth(float a, float b) {
    return std::abs(a - b) <= kSameWidthTolerancePx;
}

} // namespace

BrowserCardSizing RmlUiBrowserCardSizer::Update(const BrowserGridMetrics& metrics,
                                                const plugin_browser::BrowserCardSizeTraits& traits) {
    if (metrics.cardWidth > 0.0f) gapPx_ = metrics.cardGap;
    // 描かれているカードが、前回流した幅になっているか(流した値がDOMへ反映されるのは次のレイアウト)。
    const bool drawnAtPublished = metrics.cardWidth > 0.0f && SameWidth(metrics.cardWidth, publishedWidthPx_);

    BrowserCardSizing sizing;
    sizing.gapPx = gapPx_;
    if (metrics.innerWidth <= 0.0f) {
        // グリッドが見えていない間は前回の幅のまま。
        sizing.layout = lastLayout_;
        sizing.metricsCurrent = drawnAtPublished;
        return sizing;
    }
    sizing.layout = plugin_browser::ComputeBrowserCardLayout(metrics.innerWidth, traits, gapPx_);
    sizing.metricsCurrent = drawnAtPublished && SameWidth(sizing.layout.cardWidth, publishedWidthPx_);
    publishedWidthPx_ = sizing.layout.cardWidth;
    lastLayout_ = sizing.layout;
    return sizing;
}

} // namespace rs::app::rmlui

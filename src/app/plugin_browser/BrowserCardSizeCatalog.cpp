#include "BrowserCardSizeCatalog.h"

#include <algorithm>
#include <cmath>

namespace rs::app::plugin_browser {
namespace {

// 伸ばした幅を切り捨てる刻み(px)。
constexpr float kWidthStepPx = 0.01f;

float FloorToStep(float value) {
    return std::floor(value / kWidthStepPx) * kWidthStepPx;
}

} // namespace

std::optional<BrowserCardSize> FindBrowserCardSizeByKey(const std::string& key) {
    for (const BrowserCardSizeTraits& row : kBrowserCardSizes) {
        if (key == row.key) return row.size;
    }
    return std::nullopt;
}

BrowserCardLayout ComputeBrowserCardLayout(float innerWidth, const BrowserCardSizeTraits& traits, float gap) {
    const float available = std::max(innerWidth, 0.0f);
    BrowserCardLayout layout;
    layout.columns = std::max<std::size_t>(1, traits.columns);
    layout.cardWidth = FloorToStep(std::max(available / static_cast<float>(layout.columns) - gap, 0.0f));
    layout.thumbHeight = layout.cardWidth * kBrowserThumbHeightPerWidth;
    return layout;
}

float EstimateBrowserCardHeight(const BrowserCardLayout& layout, const BrowserCardSizeTraits& traits, float gap,
                                float dpRatio) {
    const float textHeight = (traits.nameFontDp + traits.authorFontDp) * kBrowserLineHeightPerFont * dpRatio;
    return layout.thumbHeight + textHeight + gap;
}

} // namespace rs::app::plugin_browser

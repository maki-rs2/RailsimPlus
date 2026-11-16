#include "RmlUiBrowserGridMeasure.h"

#include <algorithm>
#include <cmath>

#include <RmlUi/Core/Box.h>
#include <RmlUi/Core/Element.h>

namespace rs::app::rmlui {
namespace {

// カードの上端の位置が「同じ行」とみなせる差(px)。小数の丸めの揺れを吸収するだけの値。
constexpr float kSameRowTolerancePx = 0.5f;

constexpr const char* kCardClass = "browser-card";

} // namespace

BrowserGridMetrics MeasureBrowserGrid(Rml::Element& grid) {
    BrowserGridMetrics metrics;
    metrics.scrollTop = grid.GetScrollTop();
    metrics.viewHeight = grid.GetClientHeight();
    metrics.innerWidth = grid.GetClientWidth();

    bool firstRowOpen = true; // 1行目のカードを数えている間は真
    float rowTop = 0.0f;
    const int childCount = grid.GetNumChildren();
    for (int i = 0; i < childCount; ++i) {
        Rml::Element* const child = grid.GetChild(i);
        // data-forの元になった要素はdisplay: noneで残るので、描かれていないものは数えない。
        if (child == nullptr || !child->IsClassSet(kCardClass) || !child->IsVisible()) continue;
        const Rml::Box& box = child->GetBox();
        const float top = child->GetOffsetTop();
        const float height = box.GetSize(Rml::BoxArea::Margin).y;
        if (metrics.drawnCardCount == 0) {
            metrics.cardWidth = box.GetSize(Rml::BoxArea::Border).x;
            metrics.cardGap = box.GetSize(Rml::BoxArea::Margin).x - metrics.cardWidth;
            metrics.rowHeights.push_back(height);
            rowTop = top;
        } else if (std::abs(top - rowTop) <= kSameRowTolerancePx) {
            metrics.rowHeights.back() = std::max(metrics.rowHeights.back(), height);
        } else {
            firstRowOpen = false;
            metrics.rowHeights.push_back(height);
            rowTop = top;
        }
        if (firstRowOpen) ++metrics.columns;
        ++metrics.drawnCardCount;
    }
    return metrics;
}

} // namespace rs::app::rmlui

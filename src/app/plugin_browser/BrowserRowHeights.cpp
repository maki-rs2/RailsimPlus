#include "BrowserRowHeights.h"

#include <algorithm>
#include <cmath>

namespace rs::app::plugin_browser {

void BrowserRowHeights::Clear() {
    measured_.clear();
    measuredSum_ = 0.0;
    measuredCount_ = 0;
    ++generation_;
    stampValid_ = false;
}

void BrowserRowHeights::SetResults(const std::vector<EntryId>& results) {
    if (results == results_) return;
    results_ = results;
    Clear();
}

float BrowserRowHeights::EstimatedRowHeight() const {
    if (measuredCount_ == 0) return fallbackRowHeight_;
    return static_cast<float>(measuredSum_ / static_cast<double>(measuredCount_));
}

float BrowserRowHeights::HeightOfRow(std::size_t row) const {
    if (row < measured_.size() && measured_[row] > 0.0f) return measured_[row];
    return EstimatedRowHeight();
}

double BrowserRowHeights::TopOfRow(std::size_t row) const {
    // ComputeRowTopsと同じ順に足して、公開した上の空きと同じ値になるようにする。
    double top = 0.0;
    for (std::size_t i = 0; i < row; ++i) top += static_cast<double>(HeightOfRow(i));
    return top;
}

void BrowserRowHeights::WriteMeasured(std::size_t firstRow, std::span<const float> heights) {
    if (measured_.size() < firstRow + heights.size()) measured_.resize(firstRow + heights.size(), 0.0f);
    for (std::size_t i = 0; i < heights.size(); ++i) {
        const float height = heights[i];
        if (height <= 0.0f) continue;
        float& slot = measured_[firstRow + i];
        if (slot > 0.0f) {
            measuredSum_ -= static_cast<double>(slot);
        } else {
            ++measuredCount_;
        }
        slot = height;
        measuredSum_ += static_cast<double>(height);
    }
}

float BrowserRowHeights::Observe(const BrowserRowObservation& observation) {
    const std::size_t columns = observation.current ? observation.columns : 0;
    fallbackRowHeight_ = observation.fallbackRowHeight;
    if (!hasCondition_ || columns != columns_ || observation.cardWidthPx != cardWidthPx_ ||
        observation.size != size_) {
        Clear();
        hasCondition_ = true;
        columns_ = columns;
        cardWidthPx_ = observation.cardWidthPx;
        size_ = observation.size;
    }

    // DOMが、いま覚えている世代で公開した窓のカードを反映しているときだけ書き込む。
    const bool matched = stampValid_ && stampGeneration_ == generation_ && columns > 0 &&
                         observation.drawnCardCount == stampCount_ && stampFirst_ % columns == 0 &&
                         observation.drawnRowHeights.size() == BrowserRowCount(stampCount_, columns);
    if (!matched) return observation.scrollTop;

    const std::size_t firstRow = stampFirst_ / columns;
    WriteMeasured(firstRow, observation.drawnRowHeights);
    if (observation.scrollWasReset) return observation.scrollTop;

    // DOMの先頭の行の上端は、公開した上の空きの位置にある。新しい高さで求めた同じ行の上端との差だけ
    // スクロールをずらすと、見えている内容が飛ばない(未測の行の見積もりが変わった分も含まれる)。
    const float delta = static_cast<float>(TopOfRow(firstRow)) - stampTopSpace_;
    if (delta == 0.0f) return observation.scrollTop;
    return std::max(0.0f, std::round(observation.scrollTop + delta));
}

BrowserWindow BrowserRowHeights::Plan(float scrollTop, float viewHeight, std::size_t totalCount) {
    BrowserWindowInput input;
    input.scrollTop = scrollTop;
    input.viewHeight = viewHeight;
    input.columns = columns_;
    input.totalCount = totalCount;
    std::vector<float> heights;
    const bool hasRows = columns_ > 0 && totalCount > 0;
    if (hasRows) {
        const std::size_t rows = BrowserRowCount(totalCount, columns_);
        heights.resize(rows);
        for (std::size_t row = 0; row < rows; ++row) heights[row] = HeightOfRow(row);
        input.rowHeights = heights;
    }
    const BrowserWindow window = ComputeBrowserWindow(input);

    stampValid_ = hasRows;
    stampGeneration_ = generation_;
    stampFirst_ = window.build.first;
    stampCount_ = window.build.last - window.build.first;
    stampTopSpace_ = window.topSpace;
    return window;
}

} // namespace rs::app::plugin_browser

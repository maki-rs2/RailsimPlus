//
// RmlUiBrowserGridMeasure.h - ブラウザのカードグリッドの、スクロール位置とカードの寸法の測定
//
// DOMを読むだけで、書き換えはしない。カードの間隔と高さはRCSSが決める(幅はデータモデル経由でC++が決める)ので、
// 描かれているカードの位置と外形から、1行のカード数・各行の高さ・間隔を測る。
//
#pragma once

#include <cstddef>
#include <vector>

namespace Rml {
class Element;
} // namespace Rml

namespace rs::app::rmlui {

struct BrowserGridMetrics {
    float scrollTop = 0.0f;  // px
    float viewHeight = 0.0f; // px
    // 描かれている各行の高さ(px、カードのmargin込み。DOMの順)。同じ行のカードは高さがそろう(flexのstretch)ので、
    // 行の中で一番高いカードの外形。カードが1枚も描かれていなければ空。
    std::vector<float> rowHeights;
    std::size_t drawnCardCount = 0; // 描かれているカードの数
    std::size_t columns = 0; // 1行のカード数(1行目で数える)。未計測なら0
    float innerWidth = 0.0f; // グリッドの内側の幅(スクロールバーを除く)px
    float cardWidth = 0.0f;  // 1枚目のカードの幅(marginを除く)px。カードが無ければ0
    float cardGap = 0.0f;    // 1枚目のカードの右の間隔(margin)px。カードが無ければ0
};

// gridはid="browser-grid"の要素。描かれているカード(クラスbrowser-card)から測る。
BrowserGridMetrics MeasureBrowserGrid(Rml::Element& grid);

} // namespace rs::app::rmlui

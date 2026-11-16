//
// RmlUiBrowserCardSizer.h - プラグインブラウザのカードの幅を決め、測定値が今の幅のものかを判定する
//
// カードの幅とサムネイルの高さはデータモデルへ流す値で、
// 大きさの表(BrowserCardSizeCatalog)と、グリッドの内側の幅・カードの右の間隔(描かれたカードから測る)から
// 決める。幅を変えた直後のフレームは、DOMにはまだ前の幅で描かれたカードが残っているので、
// その測定値(列数・行の高さ)は使えない。使えるかどうかを毎回ここで判定する。
//
#pragma once

#include "../plugin_browser/BrowserCardSizeCatalog.h"
#include "RmlUiBrowserGridMeasure.h"

namespace rs::app::rmlui {

struct BrowserCardSizing {
    plugin_browser::BrowserCardLayout layout;
    // 測定値(metrics)が、今決めた幅で描かれたカードから測ったものなら真。偽なら測り直しまで未計測として扱う。
    bool metricsCurrent = false;
    float gapPx = 0.0f; // カードの右の間隔(下の余白も同じ値)。まだ測れていなければ0
};

class RmlUiBrowserCardSizer {
public:
    // metrics: 今DOMから測った値。
    BrowserCardSizing Update(const BrowserGridMetrics& metrics, const plugin_browser::BrowserCardSizeTraits& traits);

private:
    float gapPx_ = 0.0f;               // 測れたカードの右の間隔(まだ測れていなければ0)
    float publishedWidthPx_ = 0.0f;    // 前回データモデルへ流した幅
    plugin_browser::BrowserCardLayout lastLayout_;
};

} // namespace rs::app::rmlui

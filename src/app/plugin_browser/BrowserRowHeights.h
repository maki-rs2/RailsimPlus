//
// BrowserRowHeights.h - ブラウザのカードグリッドの「行ごとの高さ」の覚え書きと、スクロールの補正
//
// 名前は全文を折り返すので、カードの高さは行ごとに違う。
// 行の高さはDOMから測った値だけを覚え(文字の折り返しはC++で予測しない)、未測の行は
// 「測れた行の平均」(1行も測れていなければカード1枚ぶんの見積もり)で埋めて、窓の計算
// (ComputeBrowserWindow)へ渡す。RmlUi非依存の純C++で、DOMの測定値は数値の列として受け取る。
//
// 測った行が結果の何番目の行かは、直前に公開した窓(Plan)の build の先頭から決める。DOMは
// 「直前のRefreshで公開したカード列」を反映しているので、公開した範囲・件数と、DOMに描かれているカード数
// が合うときだけ書き込む(合わなければ捨てる)。
//
// 覚え書きを捨てる条件: 絞り込み結果が変わった・列数・カードの幅・カードの大きさ(文字の大きさもここで変わる)が変わった。
// 捨てるたびに「世代」を進め、公開した窓に付けた世代と合わないものは書き込まない。
//
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "BrowserCardSizeCatalog.h"
#include "PluginBrowserState.h"

namespace rs::app::plugin_browser {

// そのフレームにDOMから測った値と、今のカードの条件。
struct BrowserRowObservation {
    // 測定値が、いま公開している幅・大きさで描かれたカードのものなら真(偽なら未計測として扱う)。
    bool current = false;
    std::size_t columns = 0;       // 1行のカード数(currentが真のときの測定値)
    float cardWidthPx = 0.0f;      // いま決めたカードの幅
    BrowserCardSize size = kDefaultBrowserCardSize;
    std::size_t drawnCardCount = 0;           // DOMに描かれているカードの数
    std::span<const float> drawnRowHeights;   // 描かれている各行の高さ(DOMの順、margin込み)
    float scrollTop = 0.0f;                   // DOMのスクロール位置(px)
    bool scrollWasReset = false;              // このフレームに先頭へ戻した(補正しない)
    float fallbackRowHeight = 0.0f;           // 1行も測れていないときの行の高さの見積もり(EstimateBrowserCardHeight)
};

class BrowserRowHeights {
public:
    // 絞り込み結果を渡す。前回と並びが違えば覚え書きを捨てる(お気に入りの切り替えだけで結果が
    // 変わらないときは捨てない)。
    void SetResults(const std::vector<EntryId>& results);

    // DOMの測定値を覚え書きへ書き込み、上の行の高さが変わった分だけずらしたスクロール位置を返す
    // (見えている内容が飛ばないように。呼び出し側はこの値をグリッドへ設定し、読み直して Plan へ渡す)。
    float Observe(const BrowserRowObservation& observation);

    // 窓の範囲を求め、公開する窓として覚える(次の Observe で測定値の行番号を決めるため)。
    BrowserWindow Plan(float scrollTop, float viewHeight, std::size_t totalCount);

    // 未測の行に使う高さ(測れた行の平均、無ければ見積もり)。
    float EstimatedRowHeight() const;
    std::size_t MeasuredRowCount() const { return measuredCount_; }

private:
    void Clear();
    float HeightOfRow(std::size_t row) const;
    double TopOfRow(std::size_t row) const;
    void WriteMeasured(std::size_t firstRow, std::span<const float> heights);

    std::vector<EntryId> results_;
    std::vector<float> measured_; // 行番号ごとの測れた高さ(0は未測)
    double measuredSum_ = 0.0;
    std::size_t measuredCount_ = 0;
    float fallbackRowHeight_ = 0.0f;
    std::uint64_t generation_ = 0;

    // 覚え書きの前提(変わったら捨てる)。
    bool hasCondition_ = false;
    std::size_t columns_ = 0;
    float cardWidthPx_ = 0.0f;
    BrowserCardSize size_ = kDefaultBrowserCardSize;

    // 最後に公開した窓。
    bool stampValid_ = false;
    std::uint64_t stampGeneration_ = 0;
    std::size_t stampFirst_ = 0; // build.first
    std::size_t stampCount_ = 0; // build.last - build.first
    float stampTopSpace_ = 0.0f;
};

} // namespace rs::app::plugin_browser

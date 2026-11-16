//
// BrowserCardSizeCatalog.h - プラグインブラウザのカードの大きさ(大・中・小)を1つの表にまとめる
//
// 大きさごとの違い(横に並べる列数・
// 表示名・settings.datのキー)はこの表だけで持ち、ほかのコードに大きさでの分岐を書かない。
// カードの幅と列数は、グリッドの内側の幅から純関数ComputeBrowserCardLayoutで決める(RmlUi非依存)。
//
#pragma once

#include <cstddef>
#include <iterator>
#include <optional>
#include <string>

#include "../workspace/CatalogCheck.h"
#include "../workspace/EnumIndex.h"

namespace rs::app::plugin_browser {

enum class BrowserCardSize { Large, Medium, Small };

inline constexpr std::size_t kBrowserCardSizeCount = 3;

struct BrowserCardSizeTraits {
    BrowserCardSize size;
    const char* key;   // settings.datの値と、データモデル・イベントの引数
    const char* label; // セグメントボタンの表示名
    // 横に並べる列数。タブの幅によらず固定で、カードの幅は内側の幅を列数で等分して決める
    // (RCSSのカードは幅をデータモデルから受けるので、カードの寸法の出どころはこの表)。
    std::size_t columns;
    // 名前・作者の文字の大きさ[dp]。名前は全文を折り返すので、大きさが変わると行の高さも変わる。
    float nameFontDp;
    float authorFontDp;
};

inline constexpr BrowserCardSizeTraits kBrowserCardSizes[] = {
    {BrowserCardSize::Large, "large", "大", 1, 16.0f, 12.0f},
    {BrowserCardSize::Medium, "medium", "中", 2, 14.0f, 10.0f},
    {BrowserCardSize::Small, "small", "小", 3, 12.0f, 9.0f},
};

static_assert(std::size(kBrowserCardSizes) == kBrowserCardSizeCount, "kBrowserCardSizesの行数がBrowserCardSizeと一致しない");
static_assert(rs::app::workspace::IsCatalogInEnumOrder(kBrowserCardSizes, &BrowserCardSizeTraits::size),
              "kBrowserCardSizesの並びがBrowserCardSizeの列挙順と一致しない");

constexpr const BrowserCardSizeTraits& GetBrowserCardSize(BrowserCardSize size) {
    return kBrowserCardSizes[rs::app::workspace::EnumIndex(size)];
}

// 既定の大きさ。
inline constexpr BrowserCardSize kDefaultBrowserCardSize = BrowserCardSize::Medium;

// キー("large"等)から大きさを引く。表に無いキーはnullopt。
std::optional<BrowserCardSize> FindBrowserCardSizeByKey(const std::string& key);

// サムネイルの箱の高さ ÷ カードの幅(4:3)。
inline constexpr float kBrowserThumbHeightPerWidth = 3.0f / 4.0f;

// 1行の高さ ÷ 文字の大きさ。workspace.rcss の .browser-card-name / .browser-card-author の
// line-height: 1.2 と同じ値(C++の見積もりをRCSSに合わせる)。
inline constexpr float kBrowserLineHeightPerFont = 1.2f;

struct BrowserCardLayout {
    std::size_t columns = 1;
    float cardWidth = 0.0f;   // px(右の間隔は含まない)
    float thumbHeight = 0.0f; // px
};

// innerWidth: グリッドの内側の幅(px)。gap: カードの右の間隔(px)。
// カード1枚が占める幅は cardWidth + gap。列に並べても内側の幅を越えないよう
// 1/100px未満を切り捨てる(丸めで1枚が折り返さないように)。
BrowserCardLayout ComputeBrowserCardLayout(float innerWidth, const BrowserCardSizeTraits& traits, float gap);

// まだ1行も測れていないときの、カード1枚(= 1行)の高さの見積もり[px]。
// サムネイルの高さ + 名前1行 + 作者1行 + 下の余白(カードの下のmargin。RCSSでは右の間隔 --gap-x と同じ値)。
float EstimateBrowserCardHeight(const BrowserCardLayout& layout, const BrowserCardSizeTraits& traits, float gap,
                                float dpRatio);

} // namespace rs::app::plugin_browser

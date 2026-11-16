//
// BrowserSortCatalog.h - プラグインブラウザの並べ替え(名前・作者 × 昇順・降順)を1つの表にまとめる
//
// 何で並べるか・向きは、この表だけが持つ。UIは「何で」と「向き」を別々に選ぶので、
// 2つの軸から行を引く関数(FindBrowserSort)を用意する。
// 同じ値のときの順(relativeKeyの昇順)は向きに関係なく固定する。
//
#pragma once

#include <cstddef>
#include <iterator>

#include "../workspace/CatalogCheck.h"
#include "../workspace/EnumIndex.h"

namespace rs::app::plugin_browser {

enum class BrowserSortOrder { NameAscending, NameDescending, AuthorAscending, AuthorDescending };

inline constexpr std::size_t kBrowserSortOrderCount = 4;

struct BrowserSortTraits {
    BrowserSortOrder order;
    bool byAuthor; // 偽なら名前で並べる
    bool descending;
};

inline constexpr BrowserSortTraits kBrowserSorts[] = {
    {BrowserSortOrder::NameAscending, false, false},
    {BrowserSortOrder::NameDescending, false, true},
    {BrowserSortOrder::AuthorAscending, true, false},
    {BrowserSortOrder::AuthorDescending, true, true},
};

static_assert(std::size(kBrowserSorts) == kBrowserSortOrderCount, "kBrowserSortsの行数がBrowserSortOrderと一致しない");
static_assert(rs::app::workspace::IsCatalogInEnumOrder(kBrowserSorts, &BrowserSortTraits::order),
              "kBrowserSortsの並びがBrowserSortOrderの列挙順と一致しない");

constexpr const BrowserSortTraits& GetBrowserSort(BrowserSortOrder order) {
    return kBrowserSorts[rs::app::workspace::EnumIndex(order)];
}

inline constexpr BrowserSortOrder kDefaultBrowserSortOrder = BrowserSortOrder::NameAscending;

// 「何で」と「向き」の組から並べ替えを引く(表は全部の組を持つ)。
constexpr BrowserSortOrder FindBrowserSort(bool byAuthor, bool descending) {
    for (const BrowserSortTraits& row : kBrowserSorts) {
        if (row.byAuthor == byAuthor && row.descending == descending) return row.order;
    }
    return kDefaultBrowserSortOrder;
}

} // namespace rs::app::plugin_browser

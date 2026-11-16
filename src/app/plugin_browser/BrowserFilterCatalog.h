//
// BrowserFilterCatalog.h - プラグインブラウザの絞り込みの項目(セット所属・重複・信号つき)を1つの表にまとめる
//
// 項目のキー文字列(UIのイベント引数)・表示名・同時に選べない相手・使えるプロファイルの条件は、
// この表だけが持つ。選んだ項目はすべて満たすものだけを出す(AND)。
//
#pragma once

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string_view>

#include "../workspace/CatalogCheck.h"
#include "../workspace/EnumIndex.h"

namespace rs::app::plugin_browser {

enum class BrowserFilter { InSet, NotInSet, Duplicate, Signal };

inline constexpr std::size_t kBrowserFilterCount = 4;

// 選んでいる項目の集合(ビット = 1 << 列挙の添字)。
using BrowserFilterSet = std::uint32_t;

constexpr BrowserFilterSet FilterBit(BrowserFilter filter) {
    return BrowserFilterSet{1} << rs::app::workspace::EnumIndex(filter);
}

struct BrowserFilterTraits {
    BrowserFilter filter;
    const char* key;            // UIのイベント引数
    const char* label;          // 表示名
    BrowserFilterSet exclusive; // これを選ぶと外す項目(同時に選べない相手)
    bool needsSetProfile;       // レールの敷設プラグインセットを扱うプロファイルでだけ使える
};

inline constexpr BrowserFilterTraits kBrowserFilters[] = {
    {BrowserFilter::InSet, "in_set", "セットに入っている", FilterBit(BrowserFilter::NotInSet), true},
    {BrowserFilter::NotInSet, "not_in_set", "セットに入っていない", FilterBit(BrowserFilter::InSet), true},
    {BrowserFilter::Duplicate, "duplicate", "重複あり", 0, false},
    {BrowserFilter::Signal, "signal", "信号つき", 0, false},
};

static_assert(std::size(kBrowserFilters) == kBrowserFilterCount, "kBrowserFiltersの行数がBrowserFilterと一致しない");
static_assert(rs::app::workspace::IsCatalogInEnumOrder(kBrowserFilters, &BrowserFilterTraits::filter),
              "kBrowserFiltersの並びがBrowserFilterの列挙順と一致しない");

constexpr const BrowserFilterTraits& GetBrowserFilter(BrowserFilter filter) {
    return kBrowserFilters[rs::app::workspace::EnumIndex(filter)];
}

inline std::optional<BrowserFilter> FindBrowserFilterByKey(std::string_view key) {
    for (const BrowserFilterTraits& row : kBrowserFilters) {
        if (key == row.key) return row.filter;
    }
    return std::nullopt;
}

// 選んだ集合のうち、セットを扱わないプロファイルでは使えない項目を除いた集合。
constexpr BrowserFilterSet UsableFilters(BrowserFilterSet selected, bool hasSetProfile) {
    BrowserFilterSet usable = selected;
    for (const BrowserFilterTraits& row : kBrowserFilters) {
        if (row.needsSetProfile && !hasSetProfile) usable &= ~FilterBit(row.filter);
    }
    return usable;
}

} // namespace rs::app::plugin_browser

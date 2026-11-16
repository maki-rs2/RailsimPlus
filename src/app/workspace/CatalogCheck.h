//
// CatalogCheck.h - 列挙の値を1行ずつ持つ表(WorkModeCatalog/OverlayCatalog/ScreenCatalog)の共通の確認
//
// どの表も「列挙の順番に1行ずつ並べ、添字で引く」形にそろえる。並びが崩れると別の行を
// 引いてしまうため、各表でこの関数をstatic_assertに使い、コンパイル時に確かめる。
//
#pragma once

#include <cstddef>

#include "EnumIndex.h"

namespace rs::app::workspace {

// catalogの各行のkey(列挙の値を持つメンバ)が、行の番号(0,1,2…)と一致しているか。
// 例: static_assert(IsCatalogInEnumOrder(kOverlayCatalog, &OverlayTraits::kind), "…");
template <typename Row, typename Enum, std::size_t N>
constexpr bool IsCatalogInEnumOrder(const Row (&catalog)[N], Enum Row::*key) {
    for (std::size_t i = 0; i < N; ++i) {
        if (EnumIndex(catalog[i].*key) != i) return false;
    }
    return true;
}

} // namespace rs::app::workspace

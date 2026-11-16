//
// EnumIndex.h - 列挙の値を配列・表の添字(std::size_t)へ変える共通の関数
//
// 列挙の順番に並べた配列・表(左右タブ・重ね表示・画面・作業モードなど)を引くときは、
// 個別の変換関数を作らずにこれを使う。
//
#pragma once

#include <cstddef>

namespace rs::app::workspace {

template <typename E>
constexpr std::size_t EnumIndex(E value) {
    return static_cast<std::size_t>(value);
}

} // namespace rs::app::workspace

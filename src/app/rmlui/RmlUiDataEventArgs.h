//
// RmlUiDataEventArgs.h - データモデルのイベント(data-event-click等)の引数の取り出し。
//
#pragma once

#include <cstddef>

#include <RmlUi/Core/Types.h>

namespace rs::app::rmlui {

// index番目の引数をT型で取り出す。引数がそこまで無ければfallbackを返す。
// 呼び出し側ではTを明示する(例: EventArgumentAt<int>(arguments, 1, 0))。
template <typename T>
T EventArgumentAt(const Rml::VariantList& arguments, std::size_t index, T fallback) {
    return index < arguments.size() ? arguments[index].Get<T>() : fallback;
}

// 先頭の引数をT型で取り出す。引数が無ければfallbackを返す。
// 呼び出し側ではTを明示する(例: FirstEventArgument<Rml::String>(arguments, Rml::String()))。
template <typename T>
T FirstEventArgument(const Rml::VariantList& arguments, T fallback) {
    return EventArgumentAt<T>(arguments, 0, fallback);
}

} // namespace rs::app::rmlui

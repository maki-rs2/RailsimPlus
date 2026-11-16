#pragma once

#include <cstddef>
#include <optional>

#include <RmlUi/Core/Types.h>

#include "RmlUiDataEventArgs.h"
#include "../plugin_browser/PluginBrowserService.h"

namespace rs::app::rmlui {

// プラグインブラウザのビューを複数の.cppに分けたときに共有するバインド名と補助。
inline constexpr const char* kSceneryXKey = "sceneryX";
inline constexpr const char* kSceneryYKey = "sceneryY";
inline constexpr const char* kSceneryZKey = "sceneryZ";
inline constexpr const char* kSceneryYawKey = "sceneryYaw";
inline constexpr const char* kSceneryPitchKey = "sceneryPitch";
inline constexpr const char* kSceneryRollKey = "sceneryRoll";
inline constexpr const char* kMixedValue = "(複数の値)";

// イベント引数の先頭(カードのid)をカタログの添字として読む。範囲外はnullopt。
inline std::optional<plugin_browser::EntryId> ReadEntryId(const Rml::VariantList& arguments, const plugin_browser::PluginBrowserService& service) {
    const int id = FirstEventArgument<int>(arguments, -1);
    if (id < 0 || static_cast<std::size_t>(id) >= service.Catalog().Entries().size()) return std::nullopt;
    return static_cast<plugin_browser::EntryId>(id);
}

} // namespace rs::app::rmlui

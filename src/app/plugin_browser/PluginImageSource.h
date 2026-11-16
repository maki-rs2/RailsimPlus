//
// PluginImageSource.h - カードの画像をRmlUiへ渡すときのソース名
//
// RmlUiのテクスチャはソース名で引かれるので、
// ビュー(名前を出す側)とレンダラの引き先(RGBAを返す側)が同じ書式を使う。
//   ブラウザのカード: "pbimg:<世代>/<EntryId>"(変種は Standard)
//   編成カード:       "pbimg:<世代>/<EntryId>/<変種の1文字>"
// 同じ画像でも、使う側ごとに別の名前にする(片方の解放がもう片方のテクスチャを巻き込まないため)。
//
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "PluginCatalog.h"
#include "ThumbnailView.h"

namespace rs::app::plugin_browser {

enum class ImageSurface {
    BrowserGrid,   // プラグインブラウザのカード
    FormationCard, // 編成カードの車両の絵・先頭アイコン
};

struct PluginImageSource {
    std::uint64_t generation = 0;
    ImageKey key;
    ImageSurface surface = ImageSurface::BrowserGrid;
};

std::string MakeImageSourceName(const PluginImageSource& source);

// "pbimg:"で始まらない、または数字・変種の文字が読めない名前はnullopt。
// 変種の文字が無い名前はブラウザのカード(Standard)。
std::optional<PluginImageSource> ParseImageSourceName(const std::string& name);

} // namespace rs::app::plugin_browser

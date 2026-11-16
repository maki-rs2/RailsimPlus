//
// TemplateThumbnailRecipe.h - テンプレート(編成・敷設)のファイルから、サムネイルの撮影に渡すメッシュ・カメラを作る
//
// GPUには触らない(ワーカースレッドで呼ぶ)。プラグイン単体の撮り方は PluginThumbnailRecipe.h。
//
#pragma once

#include <cstdint>
#include <stop_token>
#include <string>

#include "PluginThumbnailRecipe.h"

namespace rs::app::plugin_browser {

// 編成テンプレート(TrainGroupTemplate\*.txt)の全車両を、直線の軌道へ実配置と同じ間隔で並べて撮る材料にする。
// 読めない車両は描かず、他の車両だけ描く。1両も読めなければ ok=false。
// pluginRootUtf8 が空なら、ファイルの親の親をプラグインルートとする。
// 車両を1つ読むごとに stopToken を見て、止める要求があれば ok=false で返す。
ThumbnailRecipeResult BuildTrainTemplateRecipe(const std::string& templatePathUtf8, const std::string& pluginRootUtf8,
                                               std::uint32_t width, std::uint32_t height,
                                               std::stop_token stopToken = {});

// 敷設テンプレート(RailwayPluginSet\*.txt)のセットを、敷設プラグインセットの合成と同じ絵で、カードの4:3で撮る材料にする。
// 敷設できる種別が1つも読めなければ ok=false。
ThumbnailRecipeResult BuildRailTemplateRecipe(const std::string& templatePathUtf8, const std::string& pluginRootUtf8,
                                              std::uint32_t width, std::uint32_t height,
                                              std::stop_token stopToken = {});

} // namespace rs::app::plugin_browser

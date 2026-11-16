#pragma once

#include <string>
#include <vector>

#include "LayoutLoad.h"
#include "Rs2SceneApplier.h"

namespace rs::app {

class PluginSelector;

// .rs2を中間表現へ復号する。時計・太陽・編成もNormalizedLayoutへ入れ、現在の状態には書かない
// (編成は復号中だけ選択器へ足し、戻り値の前に元へ戻す)。ライブへの反映はApplyNormalizedLayoutだけが行う。
LayoutActionResult DecodeRs2(const std::string& rs2PathUtf8, PluginSelector& selector,
                         NormalizedLayout& normalized,
                         const std::vector<std::string>& pluginRootCandidatesUtf8);

}  // namespace rs::app

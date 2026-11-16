//
// TrackPluginSetText.h - 敷設プラグインセット操作の失敗を、画面に出す文へ直す(ImGui版・RmlUi版で共用)
//
// TrackPluginSetActionの結果(TrackPluginSetResult)だけから文を作り、UIには依存しない。
//
#pragma once

#include <string>

#include "TrackPluginSetAction.h"

namespace rs::app {

// 成功なら空文字を返す。
std::string TrackPluginSetErrorText(const TrackPluginSetResult& result);

} // namespace rs::app

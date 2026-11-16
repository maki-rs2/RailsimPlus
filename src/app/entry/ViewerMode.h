//
// ViewerMode.h - プラグインビューアーモードで通常の経路と分かれる処理を1か所に集める
//
// 呼び出し側(AppRuntime*.cpp)は
// 「ビューアーかどうか」の分岐をここの関数に任せる。
//
#pragma once

#include <string>

#include "../../world/time/SolarPosition.h"

namespace rs::app {

struct AppRuntimeGlobals;
struct AppRuntimeState;

// 太陽と月の位置の計算に渡す上書き。ビューアーモードはライトの設定、それ以外はレイアウトの値
// (globals.solarOverrides、.rsl に保存される値)。太陽と月の呼び出しはどちらもこの関数を通す。
const rs::world::SolarOverrides& EffectiveSolarOverrides(const AppRuntimeGlobals& globals,
                                                          const AppRuntimeState& state);

// 起動時のアセット読込(RunStartupAssetLoads)と、開いたプラグインの登録(OpenPlugin)。
// ビューアーモードの日付・時刻・_NIGHT の初期値は、スロットの読込が終わってから入れる(OpenedPlugin.cpp)。
// 通常起動(遅延読込)と診断系CLI(同期読込)の両方がここを通る。
void RunStartupLoad(AppRuntimeGlobals& globals, AppRuntimeState& state, const std::string& openPluginPath);

} // namespace rs::app

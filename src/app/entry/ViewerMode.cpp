#include "ViewerMode.h"

#include "AppRuntime.h"
#include "OpenedPlugin.h"

namespace rs::app {

const rs::world::SolarOverrides& EffectiveSolarOverrides(const AppRuntimeGlobals& globals,
                                                          const AppRuntimeState& state) {
    return state.viewerMode ? state.viewer.solarOverrides : globals.solarOverrides;
}

void RunStartupLoad(AppRuntimeGlobals& globals, AppRuntimeState& state, const std::string& openPluginPath) {
    state.smokeReady = RunStartupAssetLoads(state.cli, globals.app, globals.camera);
    // 開いたプラグインは一時的な配置物として登録する。読込・--switch指定・ビューアーの初期スイッチ・
    // カメラ合わせは、スロットの読込が終わってからTickOpenedPluginが行う。
    if (!openPluginPath.empty()) OpenPlugin(state, openPluginPath);
}

} // namespace rs::app

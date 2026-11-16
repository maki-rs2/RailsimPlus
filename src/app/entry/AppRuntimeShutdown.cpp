//
// AppRuntimeShutdown.cpp - アプリの終了処理(AppRuntime.cpp から分離)
//
#include "AppRuntime.h"

#include <spdlog/spdlog.h>

#include "../gpu/App.h"
#include "../gpu/AppInit.h"
#include "../plugin_session/AutoRecovery.h"
#include "../scene/ParsedPluginCache.h"

namespace rs::app {

void ShutdownAppRuntime(AppRuntimeGlobals& globals, AppRuntimeState& state) {
    GetParsedPluginCache().CancelPrefetch();
    if (state.tracksCrashMarker && !state.deviceLostExit && !ClearCrashMarker(state.autoRecoveryRoot)) {
        spdlog::warn("[autorecover] could not clear crash marker");
    }
    state.audioEngine.Shutdown();
    if (UsesRmlUi(state.cli)) {
        // RmlUiIntegration::Shutdown()はGPUがアイドルなことが前提なので、解放の前に完了を待つ。
        globals.app.WaitForGpu();
        globals.app.SetRmlUiIntegration(nullptr);
        state.mainWindow.SetRmlUiIntegration(nullptr);
        state.rmlUiIntegration.Shutdown();
    }
    ShutdownGraphicsAndImGui(globals.app);
    state.mainWindow.Destroy();
    spdlog::info("clean exit");
}

} // namespace rs::app

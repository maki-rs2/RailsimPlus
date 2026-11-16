//
// StartupAssetLoad.h - 起動時のプラグイン・診断アセット読込
//
#pragma once

#include <string>
#include <vector>

namespace rs::app {

class App;
class CameraController;
struct AppCommandLine;
struct PluginSession;

struct StartupSmokeReady {
    bool instanceSmoke = false;
    bool residencySmoke = false;
    bool trainInstanceSmoke = false;
};

// CLIの--switch指定("名前=値")をsessionへ書く。解決できない指定は警告して飛ばす。
void ApplyCliSwitchOverrides(PluginSession& session, const AppCommandLine& cli);

// 診断系CLI(--scene-instance-smoke等)の準備。開いたプラグインの読込はOpenPluginが受け持つ。
StartupSmokeReady RunStartupAssetLoads(const AppCommandLine& cli, App& app, CameraController& camera);

}  // namespace rs::app

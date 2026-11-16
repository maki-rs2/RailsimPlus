#include "StartupAssetLoad.h"

#include <map>
#include <string>

#include <spdlog/spdlog.h>

#include "../gpu/App.h"
#include "../entry/AppCommandLine.h"
#include "../camera/CameraControl.h"
#include "PluginSession.h"
#include "../diagnostics/SceneInstanceSmokeTest.h"
#include "../diagnostics/SceneResidencySmokeTest.h"
#include "../diagnostics/TrainInstanceSmokeTest.h"

namespace rs::app {

namespace {

// "名前=値"または"添字=値"(定義順の0始まり)をユーザースイッチへ、"_"始まりはシステムスイッチへ書く。解決できない指定は警告して飛ばす。
void ApplySwitchOverride(PluginSession& session, const std::string& spec) {
    const size_t eq = spec.rfind('=');
    if (eq == std::string::npos || eq == 0 || eq + 1 >= spec.size()) {
        spdlog::warn("[switch] 指定が\"名前=値\"の形ではありません: {}", spec);
        return;
    }
    const std::string key = spec.substr(0, eq);
    int32_t value = 0;
    try {
        value = static_cast<int32_t>(std::stoi(spec.substr(eq + 1)));
    } catch (const std::exception&) {
        spdlog::warn("[switch] 値が整数ではありません: {}", spec);
        return;
    }
    // "_"始まりはシステムスイッチ(_APPROACH1/_STOPPING等)。参照しているものだけが表にある。
    if (key.front() == '_') {
        std::map<std::string, int32_t>::iterator it = session.SystemSwitches().find(key);
        if (it == session.SystemSwitches().end()) {
            spdlog::warn("[switch] このプラグインが参照していないシステムスイッチです: {}", key);
            return;
        }
        it->second = value;
        return;
    }
    const std::vector<rs::legacy_pi::SwitchDef>& defs = session.plugin.def.switches;
    std::string name;
    for (const rs::legacy_pi::SwitchDef& def : defs) {
        if (def.name == key) name = def.name;
    }
    if (name.empty() && key.find_first_not_of("0123456789") == std::string::npos) {
        const size_t index = static_cast<size_t>(std::stoul(key));
        if (index < defs.size()) name = defs[index].name;
    }
    if (name.empty()) {
        spdlog::warn("[switch] 該当するスイッチがありません: {}", key);
        return;
    }
    session.UserSwitches()[name] = value;
}

} // namespace

void ApplyCliSwitchOverrides(PluginSession& session, const AppCommandLine& cli) {
    for (const std::string& spec : cli.switchOverrides) ApplySwitchOverride(session, spec);
}

StartupSmokeReady RunStartupAssetLoads(const AppCommandLine& cli, App& app, CameraController& camera) {
    // --scene-instance-smoke (レンダリング基盤のみのヘッドレス検証)。
    // --scene-instance-smoke-batch が指定されていれば単一フォルダ指定より優先する。
    StartupSmokeReady ready;
    if (!cli.sceneInstanceSmokeBatchSpec.empty()) {
        const std::vector<std::pair<std::string, int>> entries =
            ParseSceneInstanceSmokeBatchSpec(cli.sceneInstanceSmokeBatchSpec);
        ready.instanceSmoke = SetupSceneInstanceSmokeBatch(app, camera, entries);
    } else if (!cli.sceneInstanceSmokePath.empty()) {
        ready.instanceSmoke =
            SetupSceneInstanceSmoke(app, camera, cli.sceneInstanceSmokePath, cli.sceneInstanceCount);
    }
    if (!cli.trainInstanceSmokePath.empty()) ready.trainInstanceSmoke = SetupTrainInstanceSmoke(app, camera, cli.trainInstanceSmokePath, cli.trainInstanceCount);
    // --scene-residency-smoke (常駐スロット経路の動的SRVアロケータ検証)。
    if (!cli.sceneResidencySmokePath.empty()) {
        ready.residencySmoke = SetupSceneResidencySmoke(app, camera, cli.sceneResidencySmokePath,
                                                         cli.sceneResidencySmokeMaxSlots);
    }
    return ready;
}

} // namespace rs::app

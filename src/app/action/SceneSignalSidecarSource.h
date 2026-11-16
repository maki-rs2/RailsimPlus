//
// SceneSignalSidecarSource.h - SceneRendererのstation.jsonキャッシュを信号プラグインの取得口として渡す
//
// SceneRendererに依存するため、単体テスト(RailsimPlusTests)には含めない。テストはISignalSidecarSourceの
// スタブを使う。
//
#pragma once

#include <string>

#include "../scene/SceneRenderer.h"
#include "SignalPluginRules.h"

namespace rs::app {

class SceneSignalSidecarSource final : public ISignalSidecarSource {
public:
    explicit SceneSignalSidecarSource(SceneRenderer& sceneRenderer) : sceneRenderer_(sceneRenderer) {}

    const rs::legacy_pi::sidecar::StationSidecar& Sidecar(const std::string& folderUtf8) override {
        return sceneRenderer_.TryGetStationSidecar(folderUtf8);
    }

    const std::vector<rs::legacy_pi::SwitchDef>* SwitchDefs(const std::string& folderUtf8) override {
        std::vector<std::string> log;
        return sceneRenderer_.TryGetSwitchDefs(folderUtf8, log);
    }

private:
    SceneRenderer& sceneRenderer_;
};

} // namespace rs::app

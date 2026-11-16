//
// SceneSignalAspectSource.h - SceneRendererの現示(SignalAspectRuntime)を信号の現示の取得口として渡す
//
// SceneRendererに依存するため、単体テスト(RailsimPlusTests)には含めない。
//
#pragma once

#include "../scene/SceneRenderer.h"
#include "SignalAspectSource.h"

namespace rs::app {

class SceneSignalAspectSource final : public ISignalAspectSource {
public:
    explicit SceneSignalAspectSource(SceneRenderer& sceneRenderer) : sceneRenderer_(sceneRenderer) {}

    rs::sim::SignalAspectRuntime& Aspects() override { return sceneRenderer_.SignalAspects(); }

private:
    SceneRenderer& sceneRenderer_;
};

} // namespace rs::app

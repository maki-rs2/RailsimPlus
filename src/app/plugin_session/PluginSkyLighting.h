//
// PluginSkyLighting.h - 現在のEnvからプラグインの空の入力(PluginSkyInput)を作る
//
// Envの Sun.Lighting.Set を太陽高度で補間した直射色・環境光色と、キューブ一式を読み終えているかを
// まとめて、レンダラ(FrameRenderer::SetPluginSkyInput)へ渡す形にする。キューブのSRV番号はここでは
// 扱わない(レンダラが EnvCubeSet から補う)。
//
#pragma once

#include "ActiveEnv.h"
#include "EnvCubeController.h"
#include "../../render/sky/EnvironmentState.h"

namespace rs::app {

// sunAltSin は sin(太陽高度)(= 観測者から見た太陽方向のY成分)。
rs::render::PluginSkyInput MakePluginSkyInput(const ActiveEnv& activeEnv, const EnvCubeController& cubes,
                                              double sunAltSin);

} // namespace rs::app

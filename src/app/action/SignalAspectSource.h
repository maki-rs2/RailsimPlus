//
// SignalAspectSource.h - 信号の現示(実行時状態)を取得する口
//
// 実体はSceneRenderer(SceneSignalAspectSource)、テストでは手元のSignalAspectRuntimeを返すスタブを渡す。
//
#pragma once

#include "../../sim/SignalAspectRuntime.h"

namespace rs::app {

class ISignalAspectSource {
public:
    virtual ~ISignalAspectSource() = default;
    virtual rs::sim::SignalAspectRuntime& Aspects() = 0;
};

} // namespace rs::app

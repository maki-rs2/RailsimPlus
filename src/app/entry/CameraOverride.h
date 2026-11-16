#pragma once

#include <string>

#include "../camera/FlyCamera.h"

namespace rs::app {

// --camera の解析結果。値の解析は初回だけ行い、適用は毎フレーム行う。
struct CameraOverrideState {
    bool parsed = false;
    bool valid = false;
    float values[6] = {};
    int count = 0;
};

// --camera の`yaw,pitch,distance[,tx,ty,tz]`をカメラへ適用する。
void ApplyCameraOverride(const std::string& spec, FlyCamera& fly, CameraOverrideState& state);

} // namespace rs::app

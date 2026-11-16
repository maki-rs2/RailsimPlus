//
// SceneryYawAction.cpp
//
#include "SceneryYawAction.h"

#include <algorithm>
#include <cmath>

namespace rs::app {

float SceneryYawAction::ClampStepDeg(float stepDeg) {
    return std::clamp(stepDeg, kMinStepDeg, kMaxStepDeg);
}

float SceneryYawAction::RotateYawDeg(float yawDeg, int steps, float stepDeg) {
    const float turned = yawDeg + static_cast<float>(steps) * stepDeg;
    const float wrapped = std::fmod(turned + 180.0f, 360.0f);
    return (wrapped < 0.0f ? wrapped + 360.0f : wrapped) - 180.0f;
}

bool SceneryYawAction::UpdateKeys(bool active, bool leftHeld, bool rightHeld, double dtSeconds,
                                  float stepDeg, float& yawDeg) {
    const int direction = (active && leftHeld != rightHeld) ? (rightHeld ? 1 : -1) : 0;
    const int steps = stepper_.Update(direction, dtSeconds);
    if (steps == 0 || stepDeg <= 0.0f) return false;
    yawDeg = RotateYawDeg(yawDeg, steps, stepDeg);
    return true;
}

} // namespace rs::app

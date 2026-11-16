//
// SceneryYawAction.h - 情景配置プレビュー中の向き(yaw)を左右矢印キーで回す
//
// 右キー=yawが増える方向(上から見て時計回り。FreePlacementのyawはY軸まわりの右手系回転)。
// 刻み角度はSceneryPlacementUiState::yawStepDegで、長押しの連続送りはKeyRepeatStepperを使う。
//
#pragma once

#include "KeyRepeatStepper.h"

namespace rs::app {

class SceneryYawAction {
public:
    // 刻み角度[度]の許容範囲(ImGui版・RmlUi版の入力欄が共用する)。
    static constexpr float kMinStepDeg = 0.1f;
    static constexpr float kMaxStepDeg = 180.0f;
    static float ClampStepDeg(float stepDeg);

    // yawDegをsteps*stepDegだけ回し、[-180, 180)へ収めて返す。
    static float RotateYawDeg(float yawDeg, int steps, float stepDeg);

    // キー1フレーム分の更新。active=falseのフレームは押下状態を捨てる。
    // 回したらtrueを返す。
    bool UpdateKeys(bool active, bool leftHeld, bool rightHeld, double dtSeconds, float stepDeg,
                    float& yawDeg);

private:
    KeyRepeatStepper stepper_;
};

} // namespace rs::app

//
// fly_camera.h - ビューカメラ（移動可能な基準点(target)を中心に回転/移動する方式）
//
#pragma once

#include "../../core/Epsilon.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace rs::app {

// ---- ビューカメラ ----
// 基準点(target)・向き(yaw/pitch)・距離(distance)を保持する。
// OrbitCamera と同じ三角関数規約(eye = target + distance*(cosP*sinYaw, sinP, cosP*cosYaw))。
// 地面判定は行わない(地面貫通・地下視点OK)。
struct FlyCamera {
    float target[3] = {0, 0, 0};  // 基準点(注視点)。エディタ操作はこの点を動かす
    float yaw = 0.0f;             // Y軸まわり。yaw=0 で +Z 方向にカメラが位置する
    float pitch = 0.4f;           // 見下ろし気味を既定に(正値=見下ろし、負値=見上げ)
    float roll = 0.0f;            // 視線まわりの傾き[rad]。正値はカメラの反時計回り。
    float distance = 30.0f;       // 基準点からカメラまでの距離
    // 垂直視野角[rad]。BuildView経由でレンダラの投影行列へ渡す。
    float fieldOfView = std::numbers::pi_v<float> / 4.0f; // 45°

    void SetRoll(double radians) {
        roll = static_cast<float>(std::remainder(radians, 2.0 * std::numbers::pi));
    }

    // カメラ→基準点方向(視線, 正規化済み)を out に書き込む。
    void Forward(float out[3]) const {
        float cp = std::cos(pitch), sp = std::sin(pitch);
        float cy = std::cos(yaw), sy = std::sin(yaw);
        // eyeOffset = distance*(cp*sy, sp, cp*cy) （target からカメラへの向き）なので
        // forward(視線=カメラ→target)はその符号反転。
        out[0] = -cp * sy;
        out[1] = -sp;
        out[2] = -cp * cy;
    }

    // eye = target - forward*distance の位置(カメラ位置)を out に書き込む。
    void EyePos(float out[3]) const {
        float cp = std::cos(pitch), sp = std::sin(pitch);
        float cy = std::cos(yaw), sy = std::sin(yaw);
        out[0] = target[0] + distance * cp * sy;
        out[1] = target[1] + distance * sp;
        out[2] = target[2] + distance * cp * cy;
    }

    // fitToAabb相当: AABB中心をtargetに、収まる距離をdistanceに設定する。
    void FrameAabb(const float mn[3], const float mx[3]) {
        for (int i = 0; i < 3; ++i) target[i] = (mn[i] + mx[i]) * 0.5f;
        float ext[3] = {mx[0] - mn[0], mx[1] - mn[1], mx[2] - mn[2]};
        float radius = 0.5f * std::sqrt(ext[0] * ext[0] + ext[1] * ext[1] +
                                        ext[2] * ext[2]);
        if (radius < rs::core::kCameraRadiusEpsM) radius = 1.0f;
        distance = radius * 2.5f;

        // OrbitCamera の既定視点(yaw=0.7, pitch=0.4)と同じ方向から見る。
        yaw = 0.7f;
        pitch = 0.4f;
    }
};

} // namespace rs::app

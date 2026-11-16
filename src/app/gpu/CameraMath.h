//
// camera_math.h - App::Render のビュー/射影行列組み立てを共有化する。
//
// LookAt/PerspectiveLH(reversed-Z) の組み立てを、app.cpp と editor コードの双方から
// 同一の行列を得られるようにここへ置く。
//
// 汎用の 4x4 行列演算(InvertMat4 / TransformPoint4)は rs::core::Math.h にある
// (LookAt/PerspectiveLH と同じ仲間であり、editor 層が app 層へ依存しないようにするため)。
//
#pragma once

#include <algorithm>

#include "../../core/Math.h"
#include "../camera/CameraView.h"

namespace rs::app {

// カメラの近クリップまでの距離[m]。射影(BuildViewProj)と、シャドウの視錐台のフィットが同じ値を使う。
constexpr float kCameraNearM = 0.02f;

struct ViewProjResult {
    rs::core::Mat4 view;
    rs::core::Mat4 proj;
    rs::core::Mat4 viewProj;
};

// CameraView(eye/target/renderDistance) + viewport(width/height) から
// App::Render と全く同じ view/proj/viewProj を組み立てる。
inline ViewProjResult BuildViewProj(const CameraView& cam, unsigned int width, unsigned int height) {
    using rs::core::LookAt;
    using rs::core::Mat4;
    using rs::core::PerspectiveLH;

    ViewProjResult r;
    r.view = LookAt(cam.eye, cam.target, cam.up);
    const float aspect = EffectiveAspect(cam, width, height);
    r.proj = PerspectiveLH(cam.fovYRadians, aspect, kCameraNearM,
                           cam.renderDistance);
    r.viewProj = Mat4::Mul(r.view, r.proj);
    return r;
}

} // namespace rs::app

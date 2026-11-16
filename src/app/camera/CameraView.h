#pragma once

namespace rs::app {

// 描画距離(遠クリップ面)の既定値。ユーザー設定の初期値になる。
constexpr float kDefaultRenderDistance = 1600.0f;
// 「描画距離」スライダの可動域。下限は近クリップ(0.02)に対して十分大きく取り、
// PerspectiveLH の 1/(zn-zf) がおかしくならないようにする。
constexpr float kRenderDistanceMin = 200.0f;
constexpr float kRenderDistanceMax = 20000.0f;

// 垂直視野角の既定値(60°)。CameraView{} で値初期化した個体が0のまま使われない
// ようフォールバックとして使う(実際の視野角はFlyCamera::fieldOfView由来で、
// UI操作で変わる。_CAMDISTも呼び出し側からその実視野角を渡して算出する)。
constexpr float kViewerFovYRadians = 1.0472f;

// カメラの種類(オービット/フライ等)に依存しない描画用ビュー情報。
// 呼び出し側(main.cpp)が OrbitCamera / FlyCamera いずれからでも組み立てて渡す。
struct CameraView {
    float eye[3];
    float target[3];
    // ユーザー設定の描画距離(遠クリップ面)。既定値を持たせているのは、CameraView{} で
    // 値初期化した個体が 0 のまま使われて手前で切られるのを防ぐため。
    float renderDistance = kDefaultRenderDistance;
    // 垂直視野角[rad]。既定値を持たせ、CameraView{} が無効な視野角にならないようにする。
    float fovYRadians = kViewerFovYRadians;
    // カメラのup方向。既定値{0,1,0}を
    // 持たせているのは、CameraView{} で値初期化した個体(eye/targetのみ書いて
    // 残りを既定に任せる呼び出し側)がworld-up固定の挙動で使われるようにするため
    // (renderDistance/fovYRadiansと同じ理由)。俯瞰カメラは傾きを反映したupを渡す。
    // カブ視点では、選択編成の先頭車体の実際のUp(カント・振り子Tilt込み)を渡す。
    float up[3] = {0.0f, 1.0f, 0.0f};
    // 投影の縦横比(幅/高さ)。0なら描画先の大きさから決める。カメラ画面でファインダーの枠に合わせるときだけ入れる。
    float aspect = 0.0f;
};

// 投影の縦横比。camに指定があればそれを、なければ描画先の幅/高さを使う。
inline float EffectiveAspect(const CameraView& cam, unsigned int width, unsigned int height) {
    if (cam.aspect > 0.0f) return cam.aspect;
    return height ? static_cast<float>(width) / static_cast<float>(height) : 1.0f;
}

} // namespace rs::app

//
// ThumbnailViewCamera.h - サムネイルのカメラ位置の決め方(GPU・プラグインに触らない純関数)
//
#pragma once

#include "ThumbnailView.h"

namespace rs::app::plugin_browser {

// 外接球・側面を収めるときの既定の余白の倍率(1.1=10%)。変種ごとの値は ThumbnailViewTraits::fitMargin。
inline constexpr float kThumbnailDefaultFitMargin = 1.1f;

// 縦の視野角[rad]の既定値(変種の表の Standard と同じ)。変種ごとの値は ThumbnailViewTraits::fovYDeg。
float ThumbnailFovYRadians();

// 撮影するメッシュの外接球(AABBの中心・半径)が、縦横の狭い方に余白つきで収まるカメラ距離[m]。
// fovYRadians は縦の視野角、aspect は 幅/高さ。radius<=0 のときは極小の半径として扱う。
float ComputeThumbnailCameraDistance(float radius, float fovYRadians, float aspect,
                                     float fitMargin = kThumbnailDefaultFitMargin);

// 側面の矩形(半分の長さ・半分の高さ)を、奥行き方向の手前の面(カメラに近い側面)の位置で
// 縦横とも余白つきで収めるカメラ距離[m](注視点=AABB中心から手前の面までは halfDepth)。
// 外接球で収めるより小さく、横長の画面に車体がいっぱいに写る。
float ComputeSideFaceCameraDistance(float halfLength, float halfHeight, float halfDepth, float fovYRadians,
                                    float aspect, float fitMargin = kThumbnailDefaultFitMargin);

// AABB(中心からの半分の大きさ half)の8隅が、toCamera の向きから見て縦横とも余白つきで収まるカメラ距離[m]。
// 長い車体を外接球で収めるより、画面いっぱいに写る。
float ComputeBoxCameraDistance(const float half[3], const float toCamera[3], float fovYRadians, float aspect);

struct ThumbnailCameraPlan {
    float eye[3] = {};
    float target[3] = {};
    float toCamera[3] = {};    // target から eye へ向かう単位ベクトル(光の向きの基準)
    float fovYRadians = 0.0f;
    float renderDistance = 0.0f;
    // カメラの方位を0(+Z 側から -Z 向き)へ合わせるための、モデルのY軸まわりの回転[rad](=使った方位)。
    // 回転後の点 (x', z') = (x cos a - z sin a, x sin a + z cos a)(a=方位)で、方位 a の向きが +Z へ移る。
    float modelYawRadians = 0.0f;
};

// ベクトルをY軸まわりに modelYawRadians の向きへ回す(位置・法線に同じく使う)。
void RotateAboutY(const float in[3], float yawRadians, float out[3]);

// メッシュのAABBから、変種ごとのカメラを決める。
// BoundingSphere: AABBの中心を見て、外接球が収まる距離に置く。
// BoundingBox: AABBの中心を見て、AABBの8隅が収まる最小の距離に置く。
// SideFace: AABBの中心を見て、側面の矩形(長さZ×高さY)が画面に収まる最小の距離に置く。
// EndFace: 前後の端面(方位が +Z 側なら aabbMax.z、-Z 側なら aabbMin.z)の中心を見て、
//          端面の断面(X・Yの広がり)が収まる距離に置く。
// TrainPhoto: 編成写真の構図。+Z 側の端(先頭)の端面が画面の高さに決まった割合で写り、先頭を右端・最後尾を
//          左端に余白つきで合わせる距離と視野角を逆算する。方位は編成の長さ(AABBのZの長さ)で補間する。
//          距離が上限に当たるときは先頭の大きさを優先し、最後尾は左端より内側に残す。
ThumbnailCameraPlan ComputeThumbnailCamera(ThumbnailView view, const float aabbMin[3], const float aabbMax[3],
                                           float aspect);

} // namespace rs::app::plugin_browser

//
// ViewConstants.h - 視点ごとの定数(メッシュのシェーダの b2)。置き場は ViewConstantsBuffer.h。
//
// 視点(メインの画面・影のカスケード・サムネイル)ごとに1枠。描画ごとに書き直す MeshConstants(b0)から分けてあるので、
// 1フレームに複数の視点を描いても、GPU が読む前に別の視点の値で上書きされない。
// DX12 の型を出さないので、描画なしのテストからもそのまま使える。
//
#pragma once

#include <cstddef>
#include <cstdint>

namespace rs::render {

// HLSL の cbuffer ViewConstants(MeshShader.h)と並びを一致させる。CBV の1枠(256バイト)ちょうど。
struct alignas(256) ViewConstants {
    float viewProj[16];  // view*proj (row-major, HLSL では転置して使用)
    float cameraPos[4];  // xyz = カメラ位置
    // 大気遠近(AP)の LUT を引くためのカメラの基底(AerialCameraFrame と同じ)。AP が無効なら使われない。
    float apRight[4];    // xyz=カメラの右 w=tanHalfFovX
    float apUp[4];       // xyz=カメラの上 w=tanHalfFovY
    float apForward[4];  // xyz=カメラの前 w=LUTの最大距離[km]
    // 前フレームの viewProj(ジッタなし)。速度の計算が使う。前フレームが無い視点は viewProj と同じ値。
    float prevViewProj[16];
    // 後続の段階(ジッタ・画面の大きさ・プローブの値)の予約欄。今はゼロのまま。
    float reserved[12];
};
static_assert(offsetof(ViewConstants, cameraPos) == 64, "HLSL gCameraPos の位置");
static_assert(offsetof(ViewConstants, apRight) == 80, "HLSL gApRight の位置");
static_assert(offsetof(ViewConstants, apUp) == 96, "HLSL gApUp の位置");
static_assert(offsetof(ViewConstants, apForward) == 112, "HLSL gApForward の位置");
static_assert(offsetof(ViewConstants, prevViewProj) == 128, "HLSL gPrevViewProj の位置");
static_assert(sizeof(ViewConstants) == 256, "ViewConstants は CBV の1枠(256バイト)");

} // namespace rs::render

//
// ShadowConstants.h - フレームに1回だけ書く影の定数バッファ(b1、ルートCBV)の中身
//
// HLSL の cbuffer ShadowConstants(ShadowShader.h)と並びを一致させる。b0 の Constants は描画ごとに
// 積むので、影の定数はこちらへ分ける。
//
#pragma once

#include <cstddef>
#include <cstdint>

#include "CascadeFit.h"

namespace rs::render {

struct alignas(256) ShadowConstants {
    // ワールド→ライトのクリップ座標(row-vector)。使うのは先頭の cascadeCount 枚。
    float viewProj[kMaxShadowCascades][16];
    // 各カスケードの奥側のカメラ前方向の距離[m]。
    float splitFar[4];
    // 各カスケードのテクセル1つのワールド幅[m]。
    float texelWorldSize[4];
    // x=影を使うか(0/1) y=影の強さ(0..1。時刻での減衰を掛けた後) z=法線オフセット[テクセル] w=フィルタの種類(ShadowFilter)
    float params[4];
    // x=シャドウ距離[m] y=遠方のフェードを始める距離[m] z=ぼけ幅(半径)[m] w=ぼかしのタップ数(0なら1タップ)
    float params2[4];
    // x=シャドウマップのSRV絶対番号 y=カスケード数 zw未使用
    std::uint32_t index[4];
    // PCSS(高): x=光源の大きさ(遮蔽物との距離に対するぼけ半径の比) y=ぼけ半径の上限[m]=遮蔽物を探す半径
    // z=遮蔽物とみなす最小の深度差[m] w=PCSSを使うか(0/1)
    float params3[4];
    // 256バイト単位(CBVの配置単位)に揃えるための埋め。
    std::uint32_t reserved[40];
};
static_assert(offsetof(ShadowConstants, splitFar) == 256, "HLSL gShadowSplitFar の位置");
static_assert(offsetof(ShadowConstants, texelWorldSize) == 272, "HLSL gShadowTexelWorld の位置");
static_assert(offsetof(ShadowConstants, params) == 288, "HLSL gShadowParams の位置");
static_assert(offsetof(ShadowConstants, params2) == 304, "HLSL gShadowParams2 の位置");
static_assert(offsetof(ShadowConstants, index) == 320, "HLSL gShadowIndex の位置");
static_assert(offsetof(ShadowConstants, params3) == 336, "HLSL gShadowParams3 の位置");
static_assert(sizeof(ShadowConstants) == 512, "256バイト単位(CBVの配置単位)");

} // namespace rs::render

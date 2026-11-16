//
// SunFlareSettings.h - 太陽のレンズフレアと白飛びの設定(Envの定義から作った、描画側が使う形)
//
// PI定義の型(plugins 層)には依存しない。定義からの変換(度→コサイン、Distance の昇順、テクスチャの
// スロット割り当て、既定値の選択)は app 層が済ませてから渡す。
//
#pragma once

#include "LensFlareMath.h"

namespace rs::render {

struct SunFlareSettings {
    // フレアを出すか。偽なら style は使わない。
    bool flare = false;
    FlareStyle style;
    // 白飛びを出すか。偽なら下の3つは使わない。
    bool whiteout = false;
    float whiteoutStartCos = 1.0f;  // StartAngle のコサイン
    FlareColor whiteoutColor;       // アルファは Whiteout の Color のアルファ(強さを掛ける前)
};

} // namespace rs::render

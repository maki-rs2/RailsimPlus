//
// PiColor.h - PI定義の色値(#AARRGGBB)を0〜1のfloatにした型。Envとエフェクタ(灯具・パーティクル)で共用する。
//
#pragma once

namespace rs::legacy_pi {

// #AARRGGBB(または#RRGGBB)を0〜1のfloatにしたもの。
struct PiColor {
    float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;
};

} // namespace rs::legacy_pi

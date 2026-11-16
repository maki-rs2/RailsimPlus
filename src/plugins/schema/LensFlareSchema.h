//
// LensFlareSchema.h - レンズフレア定義 LensFlare{} の型と読み込み
//
// Envの太陽(Sun.LensFlare)と灯具(Headlight.LensFlare)が同じ型を共用する。
// RailSim II の CLensFlare::Read / CFlareElement::Read と同じ項目を読む。
//
// 要素は宣言順の1本のリストで持つ。RailSim II は読み込み後に Distance の昇順へ並べ替えるが、
// 並べ替えは使う側(描画)で行う。パスは文字列のまま持つ(解決は使う側)。
//
#pragma once

#include <string>
#include <vector>

#include "PiColor.h"
#include "parser/Ast.h"
#include "parser/Diagnostics.h"

namespace rs::legacy_pi {

// LensFlare 内の要素1件(Circle / Hexagon / Texture)。
struct LensFlareElement {
    enum class Kind { Circle, Hexagon, Texture };
    Kind        kind = Kind::Circle;
    double      distance = 0.0;
    double      radius = 0.0;
    PiColor     innerColor;
    PiColor     outerColor;
    std::string texFileName;  // Texture のときだけ使う
};

struct LensFlareDef {
    double startAngle = 0.0;   // [度]
    double twinkle = 0.0;
    double inclination = 0.0;
    std::vector<LensFlareElement> elements;  // 宣言順
};

// LensFlare{} ブロックを読む。値の型がおかしいものは落とさず diag へ警告を出す。
// Texture の Color を省くと #ffffffff(内側・外側とも同じ値)になる。
LensFlareDef ReadLensFlare(const PiBlock& block, Diagnostics& diag);

} // namespace rs::legacy_pi

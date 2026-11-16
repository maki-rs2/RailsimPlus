//
// LensFlareSwitches.h - レンズフレア・白飛びの効果スイッチ
//
#pragma once

namespace rs::render {

// RailSim II -k-build の効果スイッチ SunLensFlare・SunWhiteout・MiscLensFlare に当たる。
// ユーザー設定(画質)から受け取り、偽のものは描かない。
struct LensFlareSwitches {
    bool sunFlare = true;    // 太陽のフレアの要素
    bool sunWhiteout = true; // 太陽の白飛び
    bool headlights = true;  // 灯具(PI の Headlight)のフレア
};

} // namespace rs::render

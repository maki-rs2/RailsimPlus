//
// EaseTable.h - 可動部の緩急(AnimationEase / ReverseEase)の曲線表
//
// 始点(0,0)・終点(1,1)の3次エルミート曲線を、時間を1000等分した表に直して引く。
// 描画非依存の純関数的な部品。
//
#pragma once

#include <array>

namespace rs::legacy_plugin {

class EaseTable {
public:
    static constexpr int kDivisions = 1000;

    // startEase / endEase は PI の `AnimationEase = a, b;` の a, b。
    //   a>0: 始点の接線ベクトル (a,0) / a<0: (0,|a|) / a=0: (0,0)
    //   b>0: 終点の接線ベクトル (0,b) / b<0: (|b|,0) / b=0: (0,0)
    EaseTable(float startEase, float endEase);

    // 進み v(0〜1、範囲外は端に丸める)に対する緩急後の進み具合を、隣り合う2点の線形補間で返す。
    float Eval(float v) const;

private:
    std::array<float, kDivisions + 1> table_{};
};

} // namespace rs::legacy_plugin

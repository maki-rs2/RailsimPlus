//
// CustomizerTiming.h - 可動部(customizer)共通のタイミング項目の型
//
#pragma once

namespace rs::legacy_pi {

// StaticMove/StaticRotation のタイミング情報(秒)。負値=未指定(既定値継承)
struct CustomizerTiming {
    float preDelay = 0, animTime = 0, postDelay = 0;
    float preRevDelay = -1, revTime = -1, postRevDelay = -1; // -1=順方向の対応値を使う
    float PreRev()  const { return preRevDelay  < 0 ? postDelay : preRevDelay; }
    float RevTime() const { return revTime      < 0 ? animTime  : revTime; }
    float PostRev() const { return postRevDelay < 0 ? preDelay  : postRevDelay; }

    // AnimationEase = a, b; / ReverseEase = a, b;(緩急の接線指定。省略=直線)
    float easeStart = 0, easeEnd = 0;
    float revEaseStart = 0, revEaseEnd = 0;
    bool  hasReverseEase = false; // false なら戻りも AnimationEase と同じ値を使う
    float RevEaseStart() const { return hasReverseEase ? revEaseStart : easeStart; }
    float RevEaseEnd()   const { return hasReverseEase ? revEaseEnd   : easeEnd; }
};

} // namespace rs::legacy_pi

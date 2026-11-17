//
// TrackingCustomizerSchema.h - 架線に追従する可動部 TrackingMove / TrackingRotation の定義の型
//
// RailSim II の CTrackingMove / CTrackingRotation が読む customizer。
// TrackingMove は StaticCustomizer の添字から実行時に参照する。TrackingRotation は値の保持のみ。
// 読み込みは ParseTrackingMove / ParseTrackingRotation(CustomizerParsers.h)。
// TrackingRotation は、TrackingID 以外が必須かどうかは未確認。省略しても捨てず、既定値で持つ。
//
#pragma once

#include <string>

#include "CustomizerTiming.h"
#include "TriggerInfo.h"

namespace rs::legacy_pi {

// TrackingMove{ TrackingID; AscentRange; SliderRange(任意); Displacement; タイミング }
struct TrackingMoveCustomizer {
    std::string trackingId;                       // 必須。追従先の名前付き Line の名前
    float ascentRange = 0.0f;                     // 必須。追従する上昇側の限界[m]
    float sliderHalfRange = -1.0f;                // SliderRange を半分にした値。省略時は -1(スリ板の幅を使わない)
    float displacement[3] = {0.0f, 0.0f, 0.0f};   // 追従先が無いときの変位
    CustomizerTiming timing;
    TriggerInfo trig;
};

// TrackingRotation{ TrackingID; RotationRadius; SliderRange(任意); RotationAxis; RotationAngle; タイミング }
struct TrackingRotationCustomizer {
    std::string trackingId;                       // 必須。追従先の名前付き Line の名前
    float rotationRadius = 0.0f;                  // 回転半径[m]
    float sliderHalfRange = -1.0f;                // SliderRange を半分にした値。省略時は -1
    float rotationAxis[3] = {0.0f, 0.0f, 1.0f};   // 省略時はローカルZ軸(StaticRotation と同じ)
    float rotationAngleRad = 0.0f;                // 度で書かれた値をラジアンに直して持つ
    CustomizerTiming timing;
    TriggerInfo trig;
};

} // namespace rs::legacy_pi

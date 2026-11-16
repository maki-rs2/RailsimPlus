//
// WindCustomizerSchema.h - 風に反応する可動部 TrackWind / Windmill の定義の型
//
// RailSim II の CWindTracker / CWindmill が読む customizer。ここでは書かれた値を保持するだけで、
// 描画にも可動部の実行時にも使わない(StaticCustomizer とは別の入れ物に持ち、MoverRuntime は読まない)。
// 読み込みは ParseWindTracker / ParseWindmill(CustomizerParsers.h)。
//
#pragma once

#include "TriggerInfo.h"

namespace rs::legacy_pi {

// TrackWind{ TrackSpeed = float; FixAxis = vector-3d(任意); }
struct WindTrackerCustomizer {
    float trackSpeed = 0.0f;   // 必須
    bool  hasFixAxis = false;  // FixAxis を書いたか
    float fixAxis[3] = {0.0f, 0.0f, 0.0f}; // 長さ1に正規化した値(長さ0なら0のまま)
    TriggerInfo trig;
};

// Windmill{ Directional = yes-no(任意); RotationAxis = vector-3d(任意); RotationSpeed = float; Symmetric = integer; }
struct WindmillCustomizer {
    bool  directional = true;               // 省略時 yes
    float rotationAxis[3] = {0.0f, 0.0f, 1.0f}; // 省略時はローカルZ軸
    float rotationSpeedRps = 0.0f;          // 必須。書かれた単位(回転/秒)のまま
    int   symmetric = 1;                    // 必須。1 未満は 1 に直す
    TriggerInfo trig;
};

} // namespace rs::legacy_pi

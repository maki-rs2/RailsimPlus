//
// EnvLighting.h - Env の Sun.Lighting.Set を太陽高度で補間する純粋関数
//
#pragma once

#include "EnvSchema.h"

namespace rs::legacy_pi {

struct EnvLightingValue {
    bool     hasLighting = false;  // Lighting.Set が1件以上あった(偽なら下の色は使わない)
    PiColor directional;          // 直射光の色
    PiColor ambient;              // 環境光の色
    PiColor skyColor;             // 背景の塗りつぶし色
};

// Lighting.Set を sunAltSin(= sin(太陽高度))で補間する。RailSim II の CEnvPlugin::Render と同じ規則:
//   表は SunAlt の昇順に並べ替え、sunAltSin が最小以下なら最初の行、最大以上なら最後の行、
//   間なら隣り合う2行の線形補間(色の4成分とも)。
// Lighting.Set が無い env は hasLighting=false。
EnvLightingValue EvaluateEnvLighting(const EnvDefinition& env, float sunAltSin);

// プラグインの空の「光の倍率」。メッシュの焼き済みの拡散・鏡面キューブに掛けるRGB
// (空パスの見える背景には使わない。そちらは Directional だけを掛ける)。
// RailSim II の Landscape のアンビエント色と同じ max(Directional, Ambient) を、チャンネルごとに取る。色はLighting.Setの値のまま(sRGB)。sRGB→リニアは単調増加なので、
// 変換の前後どちらでmaxを取っても同じ結果になる(変換は MakeEnvironmentState が1か所で行う)。
// Lighting.Set が無い(hasLighting=false)ときは (1,1,1)。a は常に1。
PiColor EvaluateEnvMeshLightScale(const EnvLightingValue& lighting);

} // namespace rs::legacy_pi

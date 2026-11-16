//
// SceneSharedSwitches.h - シーン内の全配置で同じ値になるシステムスイッチ(_NIGHT/_SEASON/_SHADOW/_ENVMAP)の値
//
// スロット経路(PluginSwitchTable::UpdateTimeSwitches)と静的インスタンス経路
// (バリアントの切り替え)が同じ関数でこの値を求めるので、2つの経路で食い違わない。
// GPU・プラグイン読込に依存しない純関数。
//
#pragma once

#include <cstdint>

#include "GameClock.h"
#include "SolarPosition.h"

namespace rs::world {

struct SceneSharedSwitchValues {
    int32_t night = 0;  // _NIGHT: 0=昼, 1=夜
    int32_t season = 0; // _SEASON: 0=春, 1=夏, 2=秋, 3=冬
    int32_t shadow = 0; // _SHADOW: 0=影を描かない, 1=影を描く
    int32_t envMap = 0; // _ENVMAP: 0=IBLを使わない, 1=使う

    bool operator==(const SceneSharedSwitchValues& other) const = default;
};

// 夜は sun.directionToSunY(=sin(太陽高度)、弧度ではない)がnightThresholdSin未満のとき。
// 季節は clock.Month() から求め(3-5月=春、6-8月=夏、9-11月=秋、12-2月=冬)、
// 緯度が負(南半球)なら季節が逆転する(RailSim II 仕様)。
// shadowEnabled は影を実際に描く設定か、envMapEnabled は実際にIBLを使っているか
// (どちらも渡し忘れを防ぐため既定値は付けない)。
// envMapEnabled は描画側の状態なので、描画の1フレーム目は偽で、IBLが使える状態になると真に変わる
// (静的配置のバリアントはそのとき選び直される)。
SceneSharedSwitchValues ComputeSceneSharedSwitchValues(const GameClock& clock,
                                                       const SolarState& sun,
                                                       double latitudeDeg,
                                                       double nightThresholdSin,
                                                       bool shadowEnabled,
                                                       bool envMapEnabled);

} // namespace rs::world

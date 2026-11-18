//
// SceneTrainWheelSound.h - 編成車両の車輪音（レールの継目）の通過判定。
//
// 描画フレームごとに1回、車両スロット1台ぶんの車軸が継目を跨いだかを調べ、鳴らす要求を集める。
// 車軸ごとの距離は「編成の通算走行距離 - 編成先頭から車軸までの距離」で求めるので、
// 車軸ごとの状態は持たない（車両ごとに前回の編成の通算走行距離だけを覚える）。
//
#pragma once

#include <vector>

#include "SceneSlotPool.h"
#include "../plugin_session/RailSoundLookup.h"
#include "../../sim/audio/WheelSoundVoicePool.h"

namespace rs::world {
struct TrackGraph;
} // namespace rs::world

namespace rs::app {

// 1フレームぶんの共通入力。
struct WheelSoundFrame {
    TrackPluginFinder findRail; // フォルダ名から読込済みのレール系エントリを引く
    double frameMs = 0.0;       // 実フレーム時間[ms]
    bool audible = false;       // false の間は覚えている距離だけ進め、要求は出さない
};

// slotの車軸が前回の描画から今回までに継目を跨いだかを調べ、鳴らす要求をoutへ足す。
// odometerMはこの編成の現在の通算走行距離。スロットが新しく割り当てられたとき・別の編成に
// なったとき・描画対象から外れていた後は、今の距離を覚えるだけで鳴らさない（誤発音の防止）。
void CollectCarWheelSounds(SceneSlotPool::Slot& slot, const rs::world::TrackGraph& graph,
                           double odometerM, const WheelSoundFrame& frame,
                           std::vector<rs::sim::WheelSoundEvent>& out);

} // namespace rs::app

//
// Rs2TrainRunStateRestore.h - .rs2に保存された編成の走行状態(State=2/3)を、
// 停車した状態のランタイムのスナップショットへ変換する。
//
// 保存時点で駅に停まっていた編成が、読込直後に走り出さないようにするための変換。
// .rslの復元(TrainRuntime::Restore / DiaRuntime::Restore)と同じ経路に流すので、
// ここでは新しい状態を作らず、既存のスナップショットを組み立てるだけにする。
//
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "../../plugins/layout/Rs2Types.h"
#include "../../sim/DiaRuntime.h"
#include "../../sim/TrainRuntime.h"
#include "../../world/alignment/TrackGraph.h"

namespace rs::app {

struct Rs2TrainRestore {
    rs::sim::TrainRuntimeSnapshot train;
    rs::sim::DiaRuntimeGroupSnapshot dia;
};

// 保存された編成のOpenDoor(左右)のどちらかが立っているか。
bool RunStateOpensAnyDoor(const rs::legacy_layout::Rs2TrainGroupRunState& runState);

// runStateが停車中(State=2)または発車直前(State=3)か。
bool IsStoppedRunState(const rs::legacy_layout::Rs2TrainGroupRunState& runState);

// runStateがState=2(停車中)/3(発車直前)なら、その停車状態を表すスナップショットを返す。
// それ以外(State=0、減速中のState=1、走行状態が無い)はnulloptで、速度だけを写す経路に任せる。
// State=1は警告を出して見送る(減速中のルーチン要素の扱いが別に要るため)。
//
// platformは足元で特定した番線(見つからなければnullptr。その場合は警告を出し、番線なしの
// 停車として始める)。nowAbsSecondsは読込直後のゲーム内時刻で、停車時間の起点になる。
// 経過時間指定(TimeType=0)の停車は経過分が保存されていないため、読込時から数え直す。
// doorCloseLeadSecondsはドアを閉じ始める先行時間[ゲーム内秒]で、保存された編成のOpenDoor
// (RunStateOpensAnyDoor)と車両のDoorClosingTimeから呼び出し側が求める。
// groupNameは警告文の識別用。
std::optional<Rs2TrainRestore> BuildRs2TrainRestore(
    const rs::legacy_layout::Rs2TrainGroupRunState& runState, rs::world::TrainId trainId,
    size_t sceneIndex, const rs::world::PlatformStopInfo* platform, double nowAbsSeconds,
    double doorCloseLeadSeconds,
    const std::string& groupName, std::vector<std::string>& warnings);

}  // namespace rs::app

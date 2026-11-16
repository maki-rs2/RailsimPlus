#pragma once

#include <cstddef>
#include <vector>
#include <unordered_set>

#include "../../world/scene/SceneRegistry.h"
#include "SceneSlotPool.h"

namespace rs::app {

using rs::world::TrainId;

struct TrainGroupSnapshot {
    TrainId trainId = rs::world::NilGuid();
    // 描画・ピッキング対象のスロットindex一覧。GPU常駐に失敗した(非常駐の)車両は
    // 対応するスロットを持たないため、この一覧には含まれない(=描画対象のみ)。
    // 性能値(trainLengthM等)の積算対象とは意味が異なる点に注意。
    std::vector<size_t> slotIndices;
    const rs::world::SceneEntity* leadEntity = nullptr;
    const rs::world::TrackPlacement* leadPlacement = nullptr;
    double trainLengthM = 0.0;
    // 編成性能は先頭車ではなく編成内全車の最大値を採る(RailSim II の CTrainGroup::CalcSpec)。
    // GPU常駐/非常駐に関わらず、編成に属する全Carエンティティを積算対象にする
    // (GPUスロットの常駐・非常駐が編成長・性能制約に影響してはならないため)。
    double maxVelocityKmh = 0.0;
    double maxAccelerationKmhPerS = 0.0;
    double maxDecelerationKmhPerS = 0.0;
    // ドアを閉じ始める先行時間の元になる、編成内全車のDoorClosingTimeの最大値[実秒]。
    double doorClosingTimeSeconds = 0.0;
    // 後退時にResolveLeadingEnd(TrainLeadingEnd.cpp)が編成後端を割り出すための
    // chase距離(TrainRuntime::SetGroupOccupiedPathKeepLength)。trainLengthMと同じく
    // GPU常駐/非常駐に関わらず編成内全車を積算対象にする(常駐車両だけを積算すると、
    // 非常駐車両を含む編成が後退する際に編成後端の位置を誤る)。
    double occupiedPathKeepLengthM = 0.0;
    // 性能値を取れない車両(GPU常駐スロットも読込済みキャッシュも無い)を含む編成。
    // 性能値が欠けたまま走らせると最高速が0扱いになり、現在速度が0へ潰れる。
    bool hasUnresolvedCars = false;
};

// 車両headから最も遠い車軸までのchase距離[m]。SolveAxles(TrainVehicle.cpp)が各
// 車軸へ与えるオフセットと同じ式で、その最大値を返す。連結限界(frontLimit -
// tailLimit)より外側に車軸を持つ車両があるため、描画可否の判定や後退時の編成後端
// 算出にはこちらを使う。
double MaxAxleChaseOffset(const rs::legacy_plugin::TrainVehicleTemplate& tmpl, bool reversed);

// activeSceneIndex は「性能値の積算から外した」警告を出すかの判断にだけ使う。アクティブSceneの
// 車両が非常駐なのは読込が終わる前の一時的な状態なので、警告は非アクティブSceneの車両に限る。
std::vector<TrainGroupSnapshot> CollectTrainGroups(
    const rs::world::SceneRegistry& registry, const std::vector<SceneSlotPool::Slot>& slots,
    const SceneSlotPool& slotPool, size_t activeSceneIndex,
    std::unordered_set<rs::world::Guid, rs::world::GuidHash>* warnedEntityIds = nullptr);

} // namespace rs::app

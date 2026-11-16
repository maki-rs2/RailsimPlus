#pragma once

#include <functional>
#include <unordered_set>
#include <vector>

#include "../../sim/BlockOccupancy.h"
#include "../../sim/SignalAspectRuntime.h"
#include "../../world/alignment/TrackGraph.h"
#include "../../world/alignment/WarpLinkTable.h"

namespace rs::app {
// ポイント在線ガードは編成ごとに自分を除いた集合が要るので、ここには持たず
// 呼び出し側がblockOccupancyTracker.Current()からBlocksOccupiedByOtherTrainsで作る。
struct SignalPhaseOutputs {
    std::unordered_set<rs::world::Guid, rs::world::GuidHash> previousOccupiedBlockGuids;
};
// 全sceneの閉塞在線を再構築する。
SignalPhaseOutputs RunSignalBlockPhase(
    const std::function<rs::world::TrackGraph*(size_t)>& graphForScene, size_t sceneCount,
    const std::vector<rs::sim::OccupancyTrain>& occupancyTrains,
    rs::sim::BlockOccupancyTracker& blockOccupancyTracker);
// 在線の再構築後に、全sceneの信号の現示を計算し直す。graphForSceneがnullptrを返すsceneで打ち切る。
// 前方の信号の探索はwarpTableのシーン間ワープを跨ぐ。
void RecomputeSignalAspects(const std::function<rs::world::TrackGraph*(size_t)>& graphForScene,
                            size_t sceneCount, const rs::world::WarpLinkTable& warpTable,
                            const rs::sim::BlockOccupancyTracker& blockOccupancyTracker,
                            rs::sim::SignalAspectRuntime& signalAspectRuntime);
} // namespace rs::app

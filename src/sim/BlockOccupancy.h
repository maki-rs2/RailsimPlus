#pragma once

#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "../world/alignment/TrackForwardScan.h"
#include "../world/alignment/TrackPosition.h"
#include "../world/alignment/WarpForwardScan.h"
#include "../world/scene/TrainId.h"
#include "TrainOccupiedPath.h"
#include "../world/alignment/Guid.h"

namespace rs::world {
struct TrackGraph;
} // namespace rs::world

namespace rs::sim {

using rs::world::TrainId;

// 在線クエリの入力(編成1本ぶん)。呼び出し側が毎tick組み立てる。
struct OccupancyTrain {
    TrainId trainId = rs::world::NilGuid();
    rs::world::TrackPosition head;
    double trainLengthM = 0.0;
    size_t sceneIndex = 0;
    const TrainOccupiedPath* occupiedPath = nullptr;
    GraphForScene graphForScene;
    // 現在の速度[km/h]。前進が正、後退が負。移動閉塞が、向かってくる相手が1tickで進む距離を
    // 見積もるのに使う(既定の0は静止として扱う)。
    double speedKmh = 0.0;
};

// 移動閉塞の探索で最も近かった在線の持ち主と、その接触点(持ち主のフットプリントの端)。
struct NearestOccupant {
    TrainId trainId = rs::world::NilGuid();
    rs::world::Guid edge = rs::world::NilGuid();
    double s = 0.0;
};

struct BlockOccupancy {
    std::vector<TrainId> occupantTrainIds;
};

using BlockOccupancySnapshot =
    std::unordered_map<rs::world::Guid, BlockOccupancy, rs::world::GuidHash>;

// 編成1本の物理フットプリント(headから-head.dirへtrainLengthM分)を占有区間として返す。
// ComputeBlockOccupancyと移動閉塞の
// 双方が使う共通ロジック。graphを書き換えない純関数。
std::vector<rs::world::OccupiedRange> ComputeTrainFootprintRanges(
    const rs::world::TrackGraph& graph, const OccupancyTrain& train, size_t sceneIndex = 0);

std::optional<double> FindNearestOccupiedAheadMAcrossScenes(
    const std::function<const rs::world::TrackGraph*(size_t)>& graphForScene,
    const rs::world::WarpLinkTable& warpTable, size_t startScene,
    const rs::world::TrackPosition& start, TrainId ownTrainId,
    const std::vector<OccupancyTrain>& trains, double maxSearchM,
    const rs::world::NodeSlotOverride* slotOverride = nullptr,
    NearestOccupant* nearest = nullptr);
std::optional<double> FindNearestOccupiedAheadMAcrossScenes(
    const std::function<const rs::world::TrackGraph*(size_t)>& graphForScene,
    const rs::world::WarpLinkTable& warpTable, size_t startScene,
    const rs::world::TrackPosition& start, TrainId ownTrainId,
    const std::vector<OccupancyTrain>& trains, double maxSearchM,
    const rs::world::ForwardScanSpan& span,
    const rs::world::NodeSlotOverride* slotOverride = nullptr,
    NearestOccupant* nearest = nullptr);

// 相手(other)が、dtSeconds秒のあいだに自分へ近づく距離[m]。移動閉塞は相手を静止物として
// 距離を測るので、向かい合う場合は相手が1tickで進む分だけ距離が実際より長く見える。
// 接触点(contact)が相手の先頭(ノーズ)なら、前進中の相手は自分へ向かってくる。接触点が相手の
// 後端なら、後退中の相手が自分へ向かってくる。それ以外(追従して離れる相手・停止中)は0。
double ClosingTravelM(const OccupancyTrain& other, const NearestOccupant& contact,
                      double dtSeconds);

// 全編成ぶんの在線判定を行う。graphを書き換えない純関数。
BlockOccupancySnapshot ComputeBlockOccupancy(const rs::world::TrackGraph& graph,
                                             const std::vector<OccupancyTrain>& trains,
                                             size_t sceneIndex = 0);

// trainId以外の編成が在線している閉塞のguid集合。ポイント在線ガード用で、
// 通過しようとしている編成自身の在線ではガードしない(RailSim IIのCRailWay::CheckRailBlockの
// `blockuser != group`と同じ)。
std::unordered_set<rs::world::Guid, rs::world::GuidHash> BlocksOccupiedByOtherTrains(
    const BlockOccupancySnapshot& snapshot, TrainId trainId);

class BlockOccupancyTracker {
public:
    void Update(BlockOccupancySnapshot next) {
        previous_ = std::move(current_);
        current_ = std::move(next);
    }
    const BlockOccupancySnapshot& Current() const { return current_; }
    const BlockOccupancySnapshot& Previous() const { return previous_; }
    // 在線の履歴を捨てる(別のレイアウトへ移るとき)。
    void Clear() {
        current_ = {};
        previous_ = {};
    }

private:
    BlockOccupancySnapshot current_;
    BlockOccupancySnapshot previous_;
};

} // namespace rs::sim

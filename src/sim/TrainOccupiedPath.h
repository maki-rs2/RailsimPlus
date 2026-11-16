//
// TrainOccupiedPath.h - 編成が実際に踏んだedge列(占有edge列)と、その上での後方解決
//
// 後方chase(ChaseBackward)はポイントの現在スロットを辿るため、先頭が通過した後に
// ポイントが切り替わると後続車両が別線へ飛ぶ。実走行で通過したedgeを順序付きで
// 保持し、後方の位置はグラフではなくこの列から解く。
//
#pragma once

#include <functional>
#include <vector>

#include "../world/alignment/TrackTraversal.h"
#include "../world/alignment/WarpForwardScan.h"
#include "../world/alignment/WarpTraversal.h"

namespace rs::sim {

// 占有edge列の1要素。dirは「headから後方へ辿る向き」で、走行方向とは逆になる。
struct OccupiedPathEntry {
    size_t sceneIndex = 0;
    rs::world::Guid edge = rs::world::NilGuid();
    int direction = +1;
    // 一つ前の要素(headに近い側)とワープで繋がっている。node同士の接続は検査しない。
    bool warpToPrevious = false;
};

// entriesはheadの在線edgeを先頭とし、後方へ向かって並ぶ。同じedgeを複数回踏む
// 経路(短いループ等)を表せるよう、Guidキーのmapではなく重複を許す順序列で持つ。
struct TrainOccupiedPath {
    std::vector<OccupiedPathEntry> entries;
    rs::world::TrackPosition head;
};

using GraphForScene = std::function<const rs::world::TrackGraph*(size_t)>;

// 列がグラフと整合しているか(edgeが存在し、隣接要素がnodeで繋がっているか)を検査する。
bool ValidateOccupiedPath(const TrainOccupiedPath& path, const GraphForScene& graphForScene);

// headからdistanceMだけ後方の位置を、グラフのポイント状態ではなく列に沿って解く。
// 列が不整合、または記録長が足りない場合はfalse(呼び出し側はグラフ上のchaseへ切り替える)。
bool ChaseBackwardOnPath(const TrainOccupiedPath& path, const GraphForScene& graphForScene,
                         double distanceM, size_t& outSceneIndex,
                         rs::world::TrackPosition& outPos);

// 占有edge列から車体占有範囲を作る。lengthM分を完全に賄えない場合、
// 列が不整合な場合は空を返す(呼び出し側はグラフ上の後方走査へ切り替える)。
std::vector<rs::world::OccupiedRangeAcrossScenes> OccupiedRangesFromPath(
    const TrainOccupiedPath& path, const GraphForScene& graphForScene, double lengthM);

// keepLengthMを賄える最小限の長さまで後端を切り詰める。
void TrimOccupiedPath(TrainOccupiedPath& path, const GraphForScene& graphForScene,
                      double keepLengthM);

// 後退走行では編成の物理的な先頭は後端側になる。新しく踏むedgeを末尾へ追記し、
// keepLengthMを賄えるまで列を延長する。
void ExtendOccupiedPathAtTail(TrainOccupiedPath& path, const GraphForScene& graphForScene,
                              const rs::world::WarpLinkTable& warpTable, double keepLengthM);

} // namespace rs::sim

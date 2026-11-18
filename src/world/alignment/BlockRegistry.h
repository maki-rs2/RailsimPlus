#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <unordered_set>
#include <vector>

#include "TrackPosition.h"
#include "WarpForwardScan.h"
#include "WarpLinkTable.h"
#include "Guid.h"
#include "SpeedSignal.h"

namespace rs::world {

struct TrackGraph;

struct BlockAheadAcrossScenes {
    size_t sceneIndex = 0;
    Guid block = NilGuid();
    double distanceM = 0.0;
};

// 閉塞とedge索引を同時に登録し、索引更新の実装を1箇所に保つ。guidが未設定なら新規発行する。
size_t RegisterBlock(TrackGraph& graph, Block block);

// graph.blocksの内容からedgeToBlocksとblockIndexById索引を作り直す。
void RebuildBlockIndices(TrackGraph& graph);

// guidの閉塞を返す。無ければnullptr。毎フレーム呼んでよい(索引を先に引く)。
const Block* FindBlock(const TrackGraph& graph, const Guid& guid);
Block* FindBlock(TrackGraph& graph, const Guid& guid);

// edgeに紐づく有効な閉塞を列挙する。
std::vector<const Block*> FindBlocksOnEdge(const TrackGraph& graph, const Guid& edge);

std::optional<BlockAheadAcrossScenes> FindNextBlockAheadAcrossScenes(
    const std::function<const TrackGraph*(size_t)>& graphForScene,
    const WarpLinkTable& warpTable, size_t startScene, TrackPosition scanStart, double maxSearchM,
    const std::unordered_set<Guid, GuidHash>& excludeBlocks,
    const NodeSlotOverride* slotOverride = nullptr);
std::optional<BlockAheadAcrossScenes> FindNextBlockAheadAcrossScenes(
    const std::function<const TrackGraph*(size_t)>& graphForScene,
    const WarpLinkTable& warpTable, size_t startScene, TrackPosition scanStart, double maxSearchM,
    const std::unordered_set<Guid, GuidHash>& excludeBlocks, const ForwardScanSpan& span,
    const NodeSlotOverride* slotOverride = nullptr);

} // namespace rs::world

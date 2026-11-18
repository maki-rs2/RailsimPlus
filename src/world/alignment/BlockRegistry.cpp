#include "BlockRegistry.h"

#include <algorithm>
#include <unordered_map>
#include <utility>

#include "WarpForwardScan.h"
#include "TrackGraph.h"

namespace rs::world {

size_t RegisterBlock(TrackGraph& graph, Block block) {
    if (block.guid == NilGuid()) block.guid = GenerateGuid();
    graph.blocks.push_back(std::move(block));
    RebuildBlockIndices(graph);
    return graph.blocks.size() - 1;
}

void RebuildBlockIndices(TrackGraph& graph) {
    graph.edgeToBlocks.clear();
    graph.blockIndexById.clear();
    for (size_t blockIndex = 0; blockIndex < graph.blocks.size(); ++blockIndex) {
        const Block& block = graph.blocks[blockIndex];
        graph.blockIndexById.try_emplace(block.guid, blockIndex);
        // 同じedgeを2回持つ閉塞でも、edgeToBlocksにはその閉塞を1回だけ載せる。
        std::vector<Guid> seen;
        for (const Guid& edge : block.edges) {
            if (std::find(seen.begin(), seen.end(), edge) != seen.end()) continue;
            seen.push_back(edge);
            graph.edgeToBlocks[edge].push_back(blockIndex);
        }
    }
}

const Block* FindBlock(const TrackGraph& graph, const Guid& guid) {
    // 毎フレーム引く箇所があるので索引を先に引く。blocksは直接書き換えられることもあるため、
    // 索引の指す先が違う・載っていないときは順に探す。
    const std::unordered_map<Guid, size_t, GuidHash>::const_iterator indexIt =
        graph.blockIndexById.find(guid);
    if (indexIt != graph.blockIndexById.end() && indexIt->second < graph.blocks.size() &&
        graph.blocks[indexIt->second].guid == guid) {
        return &graph.blocks[indexIt->second];
    }
    const std::vector<Block>::const_iterator blockIt =
        std::find_if(graph.blocks.begin(), graph.blocks.end(),
                     [&guid](const Block& block) { return block.guid == guid; });
    return blockIt == graph.blocks.end() ? nullptr : &*blockIt;
}

Block* FindBlock(TrackGraph& graph, const Guid& guid) {
    return const_cast<Block*>(FindBlock(std::as_const(graph), guid));
}

std::vector<const Block*> FindBlocksOnEdge(const TrackGraph& graph, const Guid& edge) {
    const std::unordered_map<Guid, std::vector<size_t>, GuidHash>::const_iterator blockIt =
        graph.edgeToBlocks.find(edge);
    if (blockIt == graph.edgeToBlocks.end()) return {};

    std::vector<const Block*> blocks;
    blocks.reserve(blockIt->second.size());
    for (const size_t blockIndex : blockIt->second) {
        if (blockIndex < graph.blocks.size()) {
            blocks.push_back(&graph.blocks[blockIndex]);
        }
    }
    return blocks;
}

std::optional<BlockAheadAcrossScenes> FindNextBlockAheadAcrossScenes(
    const std::function<const TrackGraph*(size_t)>& graphForScene,
    const WarpLinkTable& warpTable, size_t startScene, TrackPosition scanStart, double maxSearchM,
    const std::unordered_set<Guid, GuidHash>& excludeBlocks, const NodeSlotOverride* slotOverride) {
    const ForwardScanSpan span = ScanForwardSpanAcrossScenes(graphForScene, warpTable, startScene,
                                                              scanStart, maxSearchM, slotOverride);
    return FindNextBlockAheadAcrossScenes(graphForScene, warpTable, startScene, scanStart,
                                          maxSearchM, excludeBlocks, span, slotOverride);
}

std::optional<BlockAheadAcrossScenes> FindNextBlockAheadAcrossScenes(
    const std::function<const TrackGraph*(size_t)>& graphForScene,
    const WarpLinkTable& warpTable, size_t startScene, TrackPosition scanStart, double maxSearchM,
    const std::unordered_set<Guid, GuidHash>& excludeBlocks, const ForwardScanSpan& span,
    const NodeSlotOverride* slotOverride) {
    if (maxSearchM > span.scannedM && !span.termination.atEnd) {
        const ForwardScanSpan fallback = ScanForwardSpanAcrossScenes(
            graphForScene, warpTable, startScene, scanStart, maxSearchM, slotOverride);
        return FindNextBlockAheadAcrossScenes(graphForScene, warpTable, startScene, scanStart,
                                              maxSearchM, excludeBlocks, fallback, slotOverride);
    }
    const std::vector<OccupiedRangeAcrossScenes> ranges = ScanOccupiedRangesAheadAcrossScenes(
        graphForScene, warpTable, startScene, scanStart, maxSearchM, span, slotOverride);
    double distanceM = 0.0;
    for (const OccupiedRangeAcrossScenes& range : ranges) {
        const TrackGraph* graph = graphForScene(range.sceneIndex);
        if (graph == nullptr) continue;
        const std::vector<const Block*> blocks = FindBlocksOnEdge(*graph, range.edge);
        for (const Block* block : blocks) {
            if (excludeBlocks.contains(block->guid)) continue;
            if (range.sBegin < range.sEnd) {
                return BlockAheadAcrossScenes{range.sceneIndex, block->guid, distanceM};
            }
        }
        distanceM += range.sEnd - range.sBegin;
    }
    return std::nullopt;
}

} // namespace rs::world

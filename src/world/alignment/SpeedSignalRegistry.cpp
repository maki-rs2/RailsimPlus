#include "SpeedSignalRegistry.h"

#include <algorithm>

#include <unordered_set>

#include "BlockRegistry.h"

namespace rs::world {

void RebuildSpeedSignalIndices(TrackGraph& graph) {
    graph.edgeToSpeedSignals.clear();
    graph.blockToSpeedSignals.clear();
    for (size_t index = 0; index < graph.speedSignals.size(); ++index) {
        const SpeedSignal& signal = graph.speedSignals[index];
        graph.edgeToSpeedSignals[signal.edge].push_back(index);
        if (rs::world::IsBlockBoundarySignal(signal.kind) && signal.block != NilGuid()) {
            graph.blockToSpeedSignals[signal.block].push_back(index);
        }
    }
}

std::optional<int> SignalTopSpeedKmh(const SpeedSignal& signal) {
    if (signal.stages.empty()) return std::nullopt;
    return signal.stages.back().speedKmh;
}

const SpeedSignal* FindSpeedSignal(const TrackGraph& graph, const Guid& guid) {
    for (const SpeedSignal& signal : graph.speedSignals) {
        if (signal.guid == guid) return &signal;
    }
    return nullptr;
}

const Block* OwnerBlock(const TrackGraph& graph, const SpeedSignal& signal) {
    if (signal.block == NilGuid()) return nullptr;
    const Block* block = FindBlock(graph, signal.block);
    if (block == nullptr) return nullptr;
    if (std::find(block->edges.begin(), block->edges.end(), signal.edge) == block->edges.end()) {
        return nullptr;
    }
    return block;
}

Guid DefaultOwnerBlockOnEdge(const TrackGraph& graph, const Guid& edge) {
    // FindBlocksOnEdgeはgraph.blocksの並び順に返すので、先頭がgraph.blocksで先に並ぶ閉塞。
    const std::vector<const Block*> blocks = FindBlocksOnEdge(graph, edge);
    return blocks.empty() ? NilGuid() : blocks.front()->guid;
}

size_t PruneSignalsWithoutOwner(TrackGraph& graph) {
    const size_t before = graph.speedSignals.size();
    graph.speedSignals.erase(
        std::remove_if(graph.speedSignals.begin(), graph.speedSignals.end(),
                       [&graph](const SpeedSignal& signal) {
                           return rs::world::IsBlockBoundarySignal(signal.kind) &&
                                  OwnerBlock(graph, signal) == nullptr;
                       }),
        graph.speedSignals.end());
    RebuildSpeedSignalIndices(graph);
    return before - graph.speedSignals.size();
}

TravelDirectionMasks MarkSignalDirectionInBlock(const TrackGraph& graph,
                                                const WarpLinkTable* warpTable, size_t sceneIndex,
                                                const SpeedSignal& signal, const Block& block) {
    const std::unordered_set<Guid, GuidHash> blockEdges(block.edges.begin(), block.edges.end());
    return MarkTravelDirection(graph, warpTable, sceneIndex,
                               DirectedEdge{signal.edge, signal.direction > 0}, &blockEdges, nullptr);
}

std::vector<const SpeedSignal*> SpeedSignalsOfBlock(const TrackGraph& graph, const Block& block) {
    std::vector<const SpeedSignal*> result;
    const std::unordered_map<Guid, std::vector<size_t>, GuidHash>::const_iterator it =
        graph.blockToSpeedSignals.find(block.guid);
    if (it == graph.blockToSpeedSignals.end()) return result;
    for (const size_t signalIndex : it->second) {
        if (signalIndex >= graph.speedSignals.size()) continue;
        const SpeedSignal& signal = graph.speedSignals[signalIndex];
        if (!rs::world::IsBlockBoundarySignal(signal.kind) || signal.block != block.guid) continue;
        // 担当の閉塞がedgeを外していれば、その信号は働かない。
        if (std::find(block.edges.begin(), block.edges.end(), signal.edge) == block.edges.end()) continue;
        result.push_back(&signal);
    }
    return result;
}

SpeedSignalPlacementError ValidateSpeedSignalPlacement(const TrackGraph& graph, const Guid& edge,
                                                       double /*s*/, int direction,
                                                       const Guid& blockGuid,
                                                       const Guid& ignoreGuid,
                                                       const WarpLinkTable* warpTable,
                                                       size_t sceneIndex, SpeedSignalKind kind) {
    // 表示専用の信号は閉塞に属さないので、閉塞の検査をしない。
    if (!rs::world::IsBlockBoundarySignal(kind)) return SpeedSignalPlacementError::None;
    const Block* block = blockGuid == NilGuid() ? nullptr : FindBlock(graph, blockGuid);
    if (block == nullptr ||
        std::find(block->edges.begin(), block->edges.end(), edge) == block->edges.end()) {
        return SpeedSignalPlacementError::NoBlock;
    }
    std::vector<const SpeedSignal*> others;
    for (const SpeedSignal* signal : SpeedSignalsOfBlock(graph, *block)) {
        if (signal->guid != ignoreGuid) others.push_back(signal);
    }
    // 上下で2個そろっていれば、向きによらず「2個まで」を先に返す。
    if (others.size() >= kMaxSpeedSignalsPerBlock) return SpeedSignalPlacementError::BlockFull;
    // 他の信号から閉塞の中だけをつながりでたどり、置く位置のedgeに同じ向きの印が付くかで決める。
    // 着かないedgeは同じ向きではない。印が両方立つedge(閉塞の中の環)は、どちらの向きも拒否する。
    const unsigned char bit = direction > 0 ? kTravelAlongBit : kTravelAgainstBit;
    for (const SpeedSignal* other : others) {
        const TravelDirectionMasks masks =
            MarkSignalDirectionInBlock(graph, warpTable, sceneIndex, *other, *block);
        const TravelDirectionMasks::const_iterator it = masks.find(edge);
        if (it != masks.end() && (it->second & bit) != 0) {
            return SpeedSignalPlacementError::SameDirectionExists;
        }
    }
    return SpeedSignalPlacementError::None;
}

} // namespace rs::world

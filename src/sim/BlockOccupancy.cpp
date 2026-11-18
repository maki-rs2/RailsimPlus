#include "BlockOccupancy.h"
#include "SimTuning.h"

#include <algorithm>
#include <cmath>

#include "../core/SpeedUnits.h"
#include "../world/alignment/TrackForwardScan.h"
#include "../world/alignment/TrackGraph.h"

namespace rs::sim {

namespace {


// 在線区間とその持ち主(移動閉塞が、最も近い在線の持ち主を引くため)。
struct OwnedRange {
    rs::world::OccupiedRange range;
    TrainId owner = rs::world::NilGuid();
};
using OwnedRangesByEdge =
    std::unordered_map<rs::world::Guid, std::vector<OwnedRange>, rs::world::GuidHash>;

void UpdateNearestOccupiedDistance(bool alongEdgeDir, double entryS, double entryDistanceM,
                                   const rs::world::OccupiedRange& range, double maxSearchM,
                                   std::optional<double>& nearestDistanceM,
                                   const TrainId& owner = rs::world::NilGuid(),
                                   NearestOccupant* nearest = nullptr) {
    const double nearS = alongEdgeDir ? range.sBegin : range.sEnd;
    if ((alongEdgeDir && nearS < entryS) || (!alongEdgeDir && nearS > entryS)) return;
    const double distanceM = entryDistanceM + std::fabs(nearS - entryS);
    if (distanceM > maxSearchM) return;
    if (!nearestDistanceM.has_value() || distanceM < *nearestDistanceM) {
        nearestDistanceM = distanceM;
        if (nearest != nullptr) *nearest = NearestOccupant{owner, range.edge, nearS};
    }
}

} // namespace

std::vector<rs::world::OccupiedRange> ComputeTrainFootprintRanges(
    const rs::world::TrackGraph& graph, const OccupancyTrain& train, size_t sceneIndex) {
    if (train.occupiedPath != nullptr) {
        // graphForSceneはtrain.sceneIndex以外のsceneも解決できるため、warpを跨いで
        // 延びた占有区間をtrain.sceneIndex以外のsceneIndex呼び出しでも拾えるよう、
        // scene一致チェックより先に評価する。未設定時はsceneIndexのgraphのみに限定する
        // (単一Sceneだけのパス、非acrossScenes呼び出し向け)。
        const GraphForScene pathGraphForScene = train.graphForScene
            ? train.graphForScene
            : GraphForScene{[&graph, sceneIndex](size_t requestedSceneIndex)
                                -> const rs::world::TrackGraph* {
                                  return requestedSceneIndex == sceneIndex ? &graph : nullptr;
                              }};
        const std::vector<rs::world::OccupiedRangeAcrossScenes> pathRanges =
            OccupiedRangesFromPath(*train.occupiedPath, pathGraphForScene, train.trainLengthM);
        if (!pathRanges.empty()) {
            std::vector<rs::world::OccupiedRange> ranges;
            for (const rs::world::OccupiedRangeAcrossScenes& pathRange : pathRanges) {
                if (pathRange.sceneIndex != sceneIndex) continue;
                ranges.push_back({pathRange.edge, pathRange.sBegin, pathRange.sEnd});
            }
            return ranges;
        }
    }
    if (train.sceneIndex != sceneIndex) return {};
    rs::world::TrackPosition backwardStart = train.head;
    backwardStart.direction = -backwardStart.direction;
    return rs::world::ScanOccupiedRangesAhead(graph, backwardStart,
                                              std::max(0.0, train.trainLengthM));
}

std::optional<double> FindNearestOccupiedAheadMAcrossScenes(
    const std::function<const rs::world::TrackGraph*(size_t)>& graphForScene,
    const rs::world::WarpLinkTable& warpTable, size_t startScene,
    const rs::world::TrackPosition& start, TrainId ownTrainId,
    const std::vector<OccupancyTrain>& trains, double maxSearchM,
    const rs::world::NodeSlotOverride* slotOverride, NearestOccupant* nearest) {
    const rs::world::ForwardScanSpan span = rs::world::ScanForwardSpanAcrossScenes(
        graphForScene, warpTable, startScene, start, maxSearchM, slotOverride);
    return FindNearestOccupiedAheadMAcrossScenes(graphForScene, warpTable, startScene, start,
                                                 ownTrainId, trains, maxSearchM, span, slotOverride,
                                                 nearest);
}

std::optional<double> FindNearestOccupiedAheadMAcrossScenes(
    const std::function<const rs::world::TrackGraph*(size_t)>& graphForScene,
    const rs::world::WarpLinkTable& warpTable, size_t startScene,
    const rs::world::TrackPosition& start, TrainId ownTrainId,
    const std::vector<OccupancyTrain>& trains, double maxSearchM,
    const rs::world::ForwardScanSpan& span, const rs::world::NodeSlotOverride* slotOverride,
    NearestOccupant* nearest) {
    if (maxSearchM > span.scannedM && !span.termination.atEnd) {
        const rs::world::ForwardScanSpan fallback = rs::world::ScanForwardSpanAcrossScenes(
            graphForScene, warpTable, startScene, start, maxSearchM, slotOverride);
        return FindNearestOccupiedAheadMAcrossScenes(graphForScene, warpTable, startScene, start,
                                                     ownTrainId, trains, maxSearchM, fallback,
                                                     slotOverride, nearest);
    }
    std::optional<double> nearestDistanceM;
    const std::vector<rs::world::EdgeAheadAcrossScenes>& edgeEntries = span.entries;
    const std::vector<rs::world::OccupiedRangeAcrossScenes> scannedRanges =
        rs::world::ScanOccupiedRangesAheadAcrossScenes(graphForScene, warpTable, startScene, start,
                                                        maxSearchM, span, slotOverride);
    std::unordered_map<size_t, OwnedRangesByEdge> rangesBySceneAndEdge;
    for (const OccupancyTrain& train : trains) {
        if (train.trainId == ownTrainId) continue;
        // 占有edge列があれば、headのSceneに限らずwarpを跨いだ先Sceneの範囲まで
        // sceneIndexごとに振り分ける(train.sceneIndex一本槍だと、warpを跨いで
        // 停車中の編成の後方が別Sceneにある場合にそちらの接近編成から見落とす、
        // ComputeTrainFootprintRangesと同じ問題)。
        if (train.occupiedPath != nullptr) {
            const GraphForScene pathGraphForScene =
                train.graphForScene ? train.graphForScene : graphForScene;
            const std::vector<rs::world::OccupiedRangeAcrossScenes> pathRanges =
                OccupiedRangesFromPath(*train.occupiedPath, pathGraphForScene, train.trainLengthM);
            if (!pathRanges.empty()) {
                for (const rs::world::OccupiedRangeAcrossScenes& pathRange : pathRanges) {
                    rangesBySceneAndEdge[pathRange.sceneIndex][pathRange.edge].push_back(
                        {{pathRange.edge, pathRange.sBegin, pathRange.sEnd}, train.trainId});
                }
                continue;
            }
        }
        const rs::world::TrackGraph* trainGraph = graphForScene(train.sceneIndex);
        if (trainGraph == nullptr) continue;
        const std::vector<rs::world::OccupiedRange> ranges =
            ComputeTrainFootprintRanges(*trainGraph, train, train.sceneIndex);
        OwnedRangesByEdge& rangesByEdge = rangesBySceneAndEdge[train.sceneIndex];
        for (const rs::world::OccupiedRange& range : ranges) {
            rangesByEdge[range.edge].push_back({range, train.trainId});
        }
    }
    std::unordered_map<size_t, std::unordered_map<rs::world::Guid,
                                                   std::vector<rs::world::OccupiedRangeAcrossScenes>,
                                                   rs::world::GuidHash>>
        scannedRangesBySceneAndEdge;
    for (const rs::world::OccupiedRangeAcrossScenes& scannedRange : scannedRanges) {
        scannedRangesBySceneAndEdge[scannedRange.sceneIndex][scannedRange.edge].push_back(
            scannedRange);
    }
    for (const rs::world::EdgeAheadAcrossScenes& entry : edgeEntries) {
        const std::unordered_map<size_t, OwnedRangesByEdge>::const_iterator rangesBySceneIt =
            rangesBySceneAndEdge.find(entry.sceneIndex);
        if (rangesBySceneIt == rangesBySceneAndEdge.end()) continue;
        const OwnedRangesByEdge::const_iterator rangesIt = rangesBySceneIt->second.find(entry.edge);
        if (rangesIt == rangesBySceneIt->second.end()) continue;
        const std::unordered_map<size_t,
                                 std::unordered_map<rs::world::Guid,
                                                    std::vector<rs::world::OccupiedRangeAcrossScenes>,
                                                    rs::world::GuidHash>>::const_iterator
            scannedRangesBySceneIt = scannedRangesBySceneAndEdge.find(entry.sceneIndex);
        if (scannedRangesBySceneIt == scannedRangesBySceneAndEdge.end()) continue;
        const std::unordered_map<rs::world::Guid,
                                 std::vector<rs::world::OccupiedRangeAcrossScenes>,
                                 rs::world::GuidHash>::const_iterator scannedRangesIt =
            scannedRangesBySceneIt->second.find(entry.edge);
        if (scannedRangesIt == scannedRangesBySceneIt->second.end()) continue;
        for (const OwnedRange& owned : rangesIt->second) {
            const rs::world::OccupiedRange& range = owned.range;
            bool overlapsScannedRange = false;
            for (const rs::world::OccupiedRangeAcrossScenes& scannedRange : scannedRangesIt->second) {
                if (std::max(scannedRange.sBegin, range.sBegin) <
                    std::min(scannedRange.sEnd, range.sEnd)) {
                    overlapsScannedRange = true;
                    break;
                }
            }
            if (!overlapsScannedRange) continue;
            UpdateNearestOccupiedDistance(entry.alongEdgeDir, entry.entryS, entry.distance, range,
                                          maxSearchM, nearestDistanceM, owned.owner, nearest);
        }
    }
    return nearestDistanceM;
}

double ClosingTravelM(const OccupancyTrain& other, const NearestOccupant& contact,
                      double dtSeconds) {
    // 接触点が相手のheadと同じ位置なら、相手の先頭を最初に見つけたことになる。
    if (dtSeconds <= 0.0 || other.speedKmh == 0.0) return 0.0;
    const bool atNose = contact.edge == other.head.edge &&
                        std::fabs(contact.s - other.head.s) < SimTuning::Embedded().occupancyNoseToleranceM;
    const bool closing = other.speedKmh > 0.0 ? atNose : !atNose;
    return closing ? std::fabs(other.speedKmh) * rs::core::kKilometersPerHourToMetersPerSecond * dtSeconds : 0.0;
}

BlockOccupancySnapshot ComputeBlockOccupancy(const rs::world::TrackGraph& graph,
                                             const std::vector<OccupancyTrain>& trains,
                                             size_t sceneIndex) {
    BlockOccupancySnapshot result;
    if (graph.edgeToBlocks.empty()) return result;

    for (const OccupancyTrain& train : trains) {
        const std::vector<rs::world::OccupiedRange> ranges =
            ComputeTrainFootprintRanges(graph, train, sceneIndex);

        for (const rs::world::OccupiedRange& range : ranges) {
            const std::unordered_map<rs::world::Guid, std::vector<size_t>, rs::world::GuidHash>::const_iterator
                blockIt = graph.edgeToBlocks.find(range.edge);
            if (blockIt == graph.edgeToBlocks.end()) continue;
            for (const size_t blockIndex : blockIt->second) {
                if (blockIndex >= graph.blocks.size()) continue;
                const rs::world::Block& block = graph.blocks[blockIndex];
                // 閉塞はedge全長にかかるので、占有範囲の長さがそのまま重なりの長さ。
                if (range.sEnd - range.sBegin <= SimTuning::Embedded().occupancyOverlapToleranceM) continue;
                std::vector<TrainId>& occupants = result[block.guid].occupantTrainIds;
                if (std::find(occupants.begin(), occupants.end(), train.trainId) ==
                    occupants.end()) {
                    occupants.push_back(train.trainId);
                }
            }
        }
    }
    return result;
}

std::unordered_set<rs::world::Guid, rs::world::GuidHash> BlocksOccupiedByOtherTrains(
    const BlockOccupancySnapshot& snapshot, TrainId trainId) {
    std::unordered_set<rs::world::Guid, rs::world::GuidHash> result;
    for (const auto& [blockGuid, occupancy] : snapshot) {
        const bool occupiedByOther =
            std::any_of(occupancy.occupantTrainIds.begin(), occupancy.occupantTrainIds.end(),
                        [&trainId](const TrainId& occupant) { return occupant != trainId; });
        if (occupiedByOther) result.insert(blockGuid);
    }
    return result;
}

} // namespace rs::sim

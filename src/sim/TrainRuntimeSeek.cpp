//
// TrainRuntimeSeek.cpp
//
#include "TrainRuntime.h"
#include "SimTuning.h"

#include <algorithm>
#include <limits>
#include <unordered_set>

#include "../world/alignment/PlatformChain.h"
#include "../world/alignment/PlatformRegistry.h"
#include "../world/alignment/TrackGraph.h"

namespace rs::sim {

double TrainRuntime::ComputeStationStopDistance(
    const rs::world::TrackGraph& targetGraph,
    const rs::world::PlatformStopInfo& targetPlatform,
    const rs::world::TrackPosition& scanStart,
    const rs::world::WarpForwardScanResult& result, double maxSearchM, double trainLengthM,
    std::optional<double> stopOffsetNormalized) {
    // 駅の停止目標は、進行方向で見た番線チェーン始端からheadまでの距離。
    // チェーンが不連続などで解決不能な既存データでは、互換性のためfoundEdge中央
    // 近似へフォールバックする。
    const rs::world::TrackPosition chainStart =
        result.warpHops > 0 ? result.entryPos : scanStart;
    const rs::world::PlatformChain chain = rs::world::ResolvePlatformChain(
        targetGraph, targetPlatform, chainStart, result.foundEdge,
        maxSearchM);
    double stopDistance = 0.0;
    if (chain.ok) {
        const rs::world::PlatformStopInfo& platform = targetPlatform;
        const double offset = stopOffsetNormalized.value_or(
            rs::world::ResolveStopOffset(platform.stopReference, chain.totalLength, trainLengthM));
        stopDistance = rs::world::PlatformStopTargetDistance(
            chain, trainLengthM, offset, platform.stopOffsetM);
        if (result.warpHops > 0) stopDistance += result.distance;
    } else {
        stopDistance = result.distance;
        const std::unordered_map<rs::world::Guid, rs::world::TrackEdge,
                                 rs::world::GuidHash>::const_iterator edgeIt =
            targetGraph.edges.find(result.foundEdge);
        if (edgeIt != targetGraph.edges.end()) {
            stopDistance += rs::world::EdgeLength(edgeIt->second) * 0.5;
        }
        stopDistance += trainLengthM * 0.5;
        // 番線長がないため基準種別は解決不能でも、オフセットは設定どおり反映する。
        stopDistance += targetPlatform.stopOffsetM;
    }
    return stopDistance;
}

void TrainRuntime::SeekStationStop(
    const std::function<const rs::world::TrackGraph*(size_t)>& graphForScene,
    const rs::world::WarpLinkTable& warpTable, TrainId trainId, double maxSearchM,
    double trainLengthM, const rs::world::ForwardScanSpan* forwardSpan,
    const PlatformRoutinePeek& platformRoutinePeek,
    const PlatformRoutineConsume& platformRoutineConsume,
    const rs::world::NodeSlotOverride* slotOverride) {
    const std::map<TrainId, RunningTrain>::iterator runIt = runningGroups_.find(trainId);
    if (runIt == runningGroups_.end()) return;
    RunningTrain& running = runIt->second;
    if (!running.kin.started) return;
    const rs::world::TrackGraph* currentGraph = graphForScene(running.sceneIndex);
    if (currentGraph == nullptr) return;
    // 既に停止目標を持っている間は探索し直さない。stopDistanceは減速中も毎tick
    // 消費されて0へ収束していく値(TrainKinematics.cpp)なので、ここで再探索する
    // 必要は無い。駅が近接していると、手前の駅へ向けて減速中でも(headはまだ手前の
    // 駅のrailEdges上/通過直後)このガードが無いと、先にある駅がmaxSearchM圏内に
    // 入った瞬間に前方走査がそちらを見つけて上書きし、手前の駅で
    // 止まるはずが素通りしてしまう。
    // 駅間で走行方向が変わったら、その停止目標は既に意味を失っている(残り距離は
    // 設定時の進行方向で測った値。TrainKinematicsはstd::abs(ds)で減らすため、
    // 逆走へ転じても減り続け、駅とは無関係な地点でarrivedAtStopになる)。破棄して
    // 新しい進行方向で取り直す。
    // arrivedAtStop中(=駅に停まっている編成)は対象外にする。停車したまま逆走へ
    // 転じるのは「逆方向への発車」そのものであり、ここで破棄すると足元の番線を
    // 距離0で再捕捉して発車できなくなる。
    if (running.kin.stopDistance.has_value()) {
        if (running.kin.arrivedAtStop || TravelReversed(running) == running.stopTargetReversed) {
            return;
        }
        running.kin.stopDistance.reset();
        running.targetPlatform = rs::world::NilGuid();
    }

    ReleaseDepartedPlatformIfLeft(graphForScene, warpTable, running, trainLengthM);
    UpdatePassingPlatformLatch(graphForScene, warpTable, running.sceneIndex, running.kin.head,
                               running.occupiedPath, trainLengthM, running.passingPlatform,
                               running.passingEntered);

    std::unordered_map<size_t, std::unordered_set<rs::world::Guid, rs::world::GuidHash>>
        targetEdgesByScene;

    // speed<0(後退中)は前方探索も逆向きに行う必要がある。
    const bool reversing = TravelReversed(running);
    const LeadingEnd leadingEnd = ResolveLeadingEnd(
        graphForScene, warpTable, running.sceneIndex, running.kin.head, reversing,
        running.occupiedPath, running.occupiedPathKeepLengthM);
    const rs::world::TrackPosition scanStart = leadingEnd.position;

    const std::function<bool(size_t, const rs::world::Guid&)> isTarget =
        [&graphForScene, &targetEdgesByScene](size_t sceneIndex, const rs::world::Guid& edge) {
            std::unordered_map<size_t,
                               std::unordered_set<rs::world::Guid, rs::world::GuidHash>>::iterator
                cacheIt = targetEdgesByScene.find(sceneIndex);
            if (cacheIt == targetEdgesByScene.end()) {
                const rs::world::TrackGraph* graph = graphForScene(sceneIndex);
                if (graph == nullptr) return false;
                std::unordered_set<rs::world::Guid, rs::world::GuidHash> targetEdges;
                for (const rs::world::PlatformStopInfo& platform : graph->platforms) {
                    if (!platform.stoppable) continue;
                    for (const rs::world::Guid& railEdge : platform.railEdges) {
                        targetEdges.insert(railEdge);
                    }
                }
                cacheIt = targetEdgesByScene.emplace(sceneIndex, std::move(targetEdges)).first;
            }
            return cacheIt->second.contains(edge);
        };
    const rs::world::WarpForwardScanResult result = forwardSpan == nullptr
        ? rs::world::FindForwardEdgeAcrossScenes(
              graphForScene, warpTable, leadingEnd.sceneIndex, scanStart, isTarget, maxSearchM,
              slotOverride)
        : rs::world::FindForwardEdgeAcrossScenes(
              graphForScene, warpTable, leadingEnd.sceneIndex, scanStart, isTarget, maxSearchM,
              *forwardSpan, slotOverride);
    if (!result.found) return;

    const rs::world::TrackGraph* targetGraph = graphForScene(result.sceneIndex);
    if (targetGraph == nullptr) return;
    const std::unordered_map<rs::world::Guid, rs::world::Guid, rs::world::GuidHash>::const_iterator platformIt =
        targetGraph->edgeToPlatform.find(result.foundEdge);
    if (platformIt == targetGraph->edgeToPlatform.end()) return;
    const rs::world::PlatformStopInfo* targetPlatform =
        rs::world::FindPlatformById(*targetGraph, platformIt->second);
    if (targetPlatform == nullptr) return;
    const rs::world::Guid platformId = targetPlatform->id;

    if (platformId == running.targetPlatform) return;  // 既にターゲット済み
    if (platformId == running.departedPlatform) return;  // 発車直後の自番線再捕捉
    if (platformId == running.passingPlatform) return;  // 通過すると決めた番線の再評価

    const rs::world::PlatformRoutineElement* routineElement =
        platformRoutinePeek ? platformRoutinePeek(*targetPlatform) : nullptr;
    if (BeginPlatformPassThrough(routineElement, *targetPlatform, running.passingPlatform,
                                 running.passingEntered, platformRoutineConsume)) return;
    const std::optional<double> stopOffsetNormalized = routineElement == nullptr
        ? std::nullopt
        : routineElement->stopOffsetNormalized;
    const double stopDistance = ComputeStationStopDistance(
        *targetGraph, *targetPlatform, scanStart, result, maxSearchM, trainLengthM,
        stopOffsetNormalized);

    running.targetPlatform = platformId;
    SetGroupStopDistance(trainId, stopDistance);
}

SpeedLimitLookahead TrainRuntime::ForwardSpeedLimits(
    const std::function<const rs::world::TrackGraph*(size_t)>& graphForScene,
    const rs::world::WarpLinkTable& warpTable, TrainId trainId, double maxSearchM,
    const rs::world::ForwardScanSpan* forwardSpan,
    const rs::world::NodeSlotOverride* slotOverride,
    const rs::world::SignalSpeedForTrainFn& signalSpeed) const {
    constexpr double kUnlimited = std::numeric_limits<double>::max();
    SpeedLimitLookahead lookahead;
    const std::map<TrainId, RunningTrain>::const_iterator runIt = runningGroups_.find(trainId);
    if (runIt == runningGroups_.end() || !runIt->second.kin.started) return lookahead;

    // speed<0(後退中)は前方探索も逆向きに行う(SeekStationStopと同じ手口)。
    const LeadingEnd leadingEnd = ResolveLeadingEnd(
        graphForScene, warpTable, runIt->second.sceneIndex, runIt->second.kin.head,
        TravelReversed(runIt->second), runIt->second.occupiedPath,
        runIt->second.occupiedPathKeepLengthM);
    const rs::world::TrackPosition scanStart = leadingEnd.position;

    const std::vector<rs::world::SpeedLimitAhead> ahead = forwardSpan == nullptr
        ? rs::world::ScanSpeedLimitsAheadAcrossScenes(
              graphForScene, warpTable, leadingEnd.sceneIndex, scanStart, maxSearchM, slotOverride,
              signalSpeed)
        : rs::world::ScanSpeedLimitsAheadAcrossScenes(
              graphForScene, warpTable, leadingEnd.sceneIndex, scanStart, maxSearchM,
              *forwardSpan, slotOverride, signalSpeed);

    // 候補はedgeの先頭ごとに並ぶ。距離0の候補は今いるedgeの制限で、
    // 最高速度の上限にする。前方の候補のうち、手前の区間より制限が下がる地点だけを制約にする
    // (手前の区間の上限が先に適用されるので、同じか高い制限の地点では減速が要らない)。手前の緩い
    // 制限と奥の厳しい制限が両方前方にいる場合も、奥側が先に適用される制約として残る
    // (advisor相談で確認)。
    double zoneLimitKmh = kUnlimited;
    for (const rs::world::SpeedLimitAhead& candidate : ahead) {
        const double limitKmh =
            candidate.limitKmh < 0.0f ? kUnlimited : static_cast<double>(candidate.limitKmh);
        if (candidate.distance <= 0.0) {
            lookahead.inSectionCapKmh = std::min(lookahead.inSectionCapKmh, limitKmh);
            zoneLimitKmh = lookahead.inSectionCapKmh;
            continue;
        }
        if (limitKmh < zoneLimitKmh) {
            lookahead.ahead.push_back(rs::sim::SpeedConstraint{candidate.distance, limitKmh});
        }
        zoneLimitKmh = limitKmh;
    }
    return lookahead;
}

std::optional<double> TrainRuntime::ComputeMovingBlockConstraint(
    const std::function<const rs::world::TrackGraph*(size_t)>& graphForScene,
    const rs::world::WarpLinkTable& warpTable, TrainId trainId,
    const std::vector<OccupancyTrain>& occupancyTrains, double maxSearchM,
    const rs::world::ForwardScanSpan* forwardSpan,
    const rs::world::NodeSlotOverride* slotOverride, double dtSeconds) const {
    const std::map<TrainId, RunningTrain>::const_iterator runIt = runningGroups_.find(trainId);
    if (runIt == runningGroups_.end() || !runIt->second.kin.started) return std::nullopt;

    const LeadingEnd leadingEnd = ResolveLeadingEnd(
        graphForScene, warpTable, runIt->second.sceneIndex, runIt->second.kin.head,
        TravelReversed(runIt->second), runIt->second.occupiedPath,
        runIt->second.occupiedPathKeepLengthM);
    const rs::world::TrackPosition forwardStart = leadingEnd.position;
    NearestOccupant nearest;
    const std::optional<double> occupiedDistanceM = forwardSpan == nullptr
        ? FindNearestOccupiedAheadMAcrossScenes(
              graphForScene, warpTable, leadingEnd.sceneIndex, forwardStart, trainId,
              occupancyTrains, maxSearchM, slotOverride, &nearest)
        : FindNearestOccupiedAheadMAcrossScenes(
              graphForScene, warpTable, leadingEnd.sceneIndex, forwardStart, trainId,
              occupancyTrains, maxSearchM, *forwardSpan, slotOverride, &nearest);
    if (!occupiedDistanceM.has_value()) return std::nullopt;
    //相手が自分へ向かって動いているときは、双方が同じ隙間を使い切らないよう半分ずつ使って止まる
    //(dtが大きい時間加速でも、すれ違う前に止まる)
    double closingM = 0.0;
    for (const OccupancyTrain& other : occupancyTrains) {
        if (other.trainId == nearest.trainId) {
            closingM = ClosingTravelM(other, nearest, dtSeconds);
            break;
        }
    }
    const double marginM = SimTuning::Embedded().signalMargins.movingBlockStopMarginM;
    if (closingM > 0.0) return std::max(0.0, (*occupiedDistanceM - marginM) * 0.5);
    return std::max(0.0, *occupiedDistanceM - marginM);
}

} // namespace rs::sim

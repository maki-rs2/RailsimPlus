//
// TrainRuntimeTick.cpp
//
#include "TrainRuntime.h"
#include "../core/SpeedUnits.h"

#include <array>
#include <cmath>
#include <unordered_set>

#include "../world/alignment/PointRoutineResolve.h"
#include "../world/alignment/RailLinkOverlay.h"
#include "../world/alignment/WarpTraversal.h"
#include "../world/alignment/TrackGraph.h"

namespace rs::sim {

void TrainRuntime::SeedOccupiedPath(RunningTrain& running, const GraphForScene& graphForScene,
                                    const rs::world::WarpLinkTable& warpTable) {
    running.occupiedPath.entries.clear();
    if (running.occupiedPathKeepLengthM <= 0.0) {
        return;
    }
    rs::world::TrackPosition start = running.kin.head;
    start.direction = -start.direction;
    const rs::world::WarpAdvanceResult seed =
        rs::world::AdvanceAcrossScenes(graphForScene, warpTable, running.sceneIndex, start,
                                       running.occupiedPathKeepLengthM);
    running.occupiedPath.head = running.kin.head;
    for (const rs::world::WarpVisitedEdge& visited : seed.visitedEdges) {
        running.occupiedPath.entries.push_back(OccupiedPathEntry{
            visited.sceneIndex, visited.edge.edge, visited.edge.direction, visited.viaWarp});
    }
}

void TrainRuntime::UpdateOccupiedPathAfterAdvance(RunningTrain& running,
                                                  const rs::world::WarpAdvanceResult& result,
                                                  bool reversing,
                                                  const GraphForScene& graphForScene,
                                                  const rs::world::WarpLinkTable& warpTable) {
    TrainKinematics& kin = running.kin;
    // 占有edge列の更新: 前進時は進行方向の先頭側を実走査で伸ばす。後退時はhead側を
    // 消費した後、物理的な進行先である後端側を延長する。
    if (result.visitedEdges.empty()) {
        // 走査が1edgeも返さない(データ不整合)。列を作り直して次tickへ委ねる。
        SeedOccupiedPath(running, graphForScene, warpTable);
    } else if (reversing) {
        const rs::world::WarpVisitedEdge& last = result.visitedEdges.back();
        size_t index = 0;
        for (; index < running.occupiedPath.entries.size(); ++index) {
            const OccupiedPathEntry& entry = running.occupiedPath.entries[index];
            if (entry.sceneIndex == last.sceneIndex && entry.edge == last.edge.edge &&
                entry.direction == last.edge.direction) {
                break;
            }
        }
        if (index < running.occupiedPath.entries.size()) {
            running.occupiedPath.entries.erase(
                running.occupiedPath.entries.begin(),
                running.occupiedPath.entries.begin() + static_cast<std::ptrdiff_t>(index));
            running.occupiedPath.head = kin.head;
        } else {
            // 列に無いedgeへ出た(記録より後方へ抜けた等)。作り直す。
            SeedOccupiedPath(running, graphForScene, warpTable);
        }
    } else {
        // visitedEdges[0]は走査開始edge = 既に列の先頭にあるので除く。残りを
        // 後方向き(dirを反転)にして新しい順から前へ差し込む。
        std::vector<OccupiedPathEntry> prefix;
        prefix.reserve(result.visitedEdges.size());
        // 要素の warpToPrevious は「headに近い側の要素との間がワープか」なので、
        // 走査順で一つ後ろのedgeの viaWarp を持つ。
        for (size_t index = result.visitedEdges.size(); index > 1; --index) {
            const rs::world::WarpVisitedEdge& visited = result.visitedEdges[index - 1];
            const bool warpToPrevious =
                index < result.visitedEdges.size() && result.visitedEdges[index].viaWarp;
            prefix.push_back(OccupiedPathEntry{visited.sceneIndex, visited.edge.edge,
                                               -visited.edge.direction, warpToPrevious});
        }
        if (result.visitedEdges.size() > 1 && !running.occupiedPath.entries.empty()) {
            running.occupiedPath.entries.front().warpToPrevious = result.visitedEdges[1].viaWarp;
        }
        running.occupiedPath.entries.insert(running.occupiedPath.entries.begin(), prefix.begin(),
                                            prefix.end());
        running.occupiedPath.head = kin.head;
    }
    TrimOccupiedPath(running.occupiedPath, graphForScene, running.occupiedPathKeepLengthM);
    if (reversing) {
        ExtendOccupiedPathAtTail(running.occupiedPath, graphForScene, warpTable,
                                 running.occupiedPathKeepLengthM);
    }
}

void TrainRuntime::WriteBackTrailingPasses(
    const std::function<rs::world::TrackGraph*(size_t)>& graphForScene,
    const rs::world::WarpAdvanceResult& result) {
    // 割り出し(trailing)の書き戻し:
    // 前進するheadがポイントへ背向進入したscene/node/面について、現在スロットを
    // 進入スロットへ更新する。これで後方chase(TrainVehicle::SolveAxles)が
    // 同じ経路を再導出できるようになる。後方chaseは自身のAdvance結果の
    // trailingPassesを単に無視しており、ここ(前進するheadの走査)だけが
    // グラフへ適用する唯一の経路。
    for (const rs::world::WarpTrailingPass& warpPass : result.trailingPasses) {
        rs::world::TrackGraph* graph = graphForScene(warpPass.sceneIndex);
        if (graph == nullptr) continue;
        rs::world::TrackNode* node = rs::world::FindLinkNodeMutable(*graph, warpPass.pass.node);
        if (node == nullptr) continue;
        node->activeSlot[static_cast<size_t>(warpPass.pass.side)] = warpPass.pass.slot;
    }
}

// 条件レイヤ導入前の呼び出し元(既存テスト等)向けの後方互換オーバーロード。条件レイヤは
// 常に空文脈(タグ無し・trainId不明扱い・在線条件も時刻条件も一致しない)
// で評価される。
bool TrainRuntime::TickGroup(const std::function<rs::world::TrackGraph*(size_t)>& graphForScene,
                               const rs::world::WarpLinkTable& warpTable, size_t currentSceneIndex,
                               double dt, TrainId trainId, const rs::world::TrackPosition& leadPosition,
                               double maxVelocityKmh, double maxAccelerationKmhPerS,
                               double maxDecelerationKmhPerS, rs::world::TrackPosition& outHead,
                               double& outDsMeters, size_t& outSceneIndex,
                               const std::unordered_set<rs::world::Guid, rs::world::GuidHash>& occupiedBlocks) {
    static const std::vector<int> kNoTags;
    static const std::unordered_set<rs::world::Guid, rs::world::GuidHash> kNoPreviousBlocks;
    return TickGroup(graphForScene, warpTable, currentSceneIndex, dt, trainId, leadPosition,
                     maxVelocityKmh, maxAccelerationKmhPerS, maxDecelerationKmhPerS, outHead,
                     outDsMeters, outSceneIndex, occupiedBlocks, kNoTags, rs::world::NilGuid(),
                     0.0, kNoPreviousBlocks,
                     nullptr);
}

bool TrainRuntime::TickGroup(const std::function<rs::world::TrackGraph*(size_t)>& graphForScene,
                               const rs::world::WarpLinkTable& warpTable, size_t currentSceneIndex,
                               double dt, TrainId trainId, const rs::world::TrackPosition& leadPosition,
                               double maxVelocityKmh, double maxAccelerationKmhPerS,
                               double maxDecelerationKmhPerS, rs::world::TrackPosition& outHead,
                               double& outDsMeters, size_t& outSceneIndex,
                               const std::unordered_set<rs::world::Guid, rs::world::GuidHash>& occupiedBlocks,
                               const std::vector<int>& trainTagIds,
                               const rs::world::Guid& formationId, double nowAbsSeconds,
                               const std::unordered_set<rs::world::Guid, rs::world::GuidHash>&
                                   previousOccupiedBlocks,
                               const rs::world::ForwardScanSpan* forwardSpan) {
    const std::map<TrainId, RunningTrain>::iterator runIt = runningGroups_.find(trainId);
    if (runIt == runningGroups_.end()) return false;

    RunningTrain& running = runIt->second;
    TrainKinematics& kin = running.kin;
    if (!kin.started) {
        // SetGroupTargetSpeedでの新規エントリはheadが未初期化(デフォルト構築)
        // のままなので、実際の先頭車位置で初期化してからでないとAdvanceが
        // 存在しないedgeを参照して落ちる。新しく走り出す編成は、その時点の
        // アクティブSceneに属する。
        // currentSceneIndexにはleadPositionが属するSceneの添字(呼び出し側が
        // SceneEntity::placementSceneIndexとして保持しているタグ)を渡すこと。
        // アクティブSceneの添字で代用すると、非アクティブSceneにいる編成が
        // 別Sceneのgraphに対してAdvanceされ、edge Guidが見つからず永久停止する。
        kin.head = leadPosition;
        kin.started = true;
        running.odometerM = 0.0;
        running.sceneIndex = currentSceneIndex;
    }

    // 占有edge列は軌道編集で古くなりうるため、毎tick先頭でグラフとの整合を検査し、
    // 壊れていれば作り直す(remapは持たず、head起点の後方走査から張り直す)。
    const GraphForScene constGraphForScene =
        [&graphForScene](size_t sceneIndex) -> const rs::world::TrackGraph* {
        return graphForScene(sceneIndex);
    };
    if (!ValidateOccupiedPath(running.occupiedPath, constGraphForScene)) {
        SeedOccupiedPath(running, constGraphForScene, warpTable);
    }

    // 速度更新(加減速クランプ)はTrainKinematics側のロジックを再利用し、
    // 前進計算のみwarp対応のAdvanceAcrossScenesへ切り替える
    // (クランプ計算はUpdateTrainSpeedに一本化している)。
    std::array<std::optional<double>, static_cast<size_t>(rs::sim::StopConstraintSource::Count)>
        externalOnly = running.stopConstraints;
    externalOnly[static_cast<size_t>(rs::sim::StopConstraintSource::StationStop)] = std::nullopt;
    const std::optional<double> externalStopConstraintM = rs::sim::MinStopConstraintM(externalOnly);
    const double ds = UpdateTrainSpeed(dt, maxVelocityKmh, maxAccelerationKmhPerS,
                                         maxDecelerationKmhPerS, kin, externalStopConstraintM,
                                         running.speedLimitConstraints);

    // "_ACCEL"用の加減速度[m/h/s](RailSim II 仕様の単位)を、速度の絶対値のtick間差分から
    // 算出する(丸めなし。丸めは書き込み側=PluginSessionで行う)。
    const double newSpeedKmh = rs::core::MetersPerSecondToKilometersPerHour(kin.speed);
    if (dt > 0.0) {
        running.accelMhPerS =
            (std::fabs(newSpeedKmh) - std::fabs(running.prevSpeedKmh)) * 1000.0 / dt;
    }
    running.prevSpeedKmh = newSpeedKmh;

    if (ds == 0.0) {
        kin.atEnd = false;
        outHead = kin.head;
        outDsMeters = 0.0;
        outSceneIndex = running.sceneIndex;
        return true;
    }

    // ds<0(後退)は、dirを反転したTrackPositionでAdvanceAcrossScenesを
    // 呼んだ後にdir表記を元へ戻す(rs::sim::ChaseBackwardと同じ手口)。この向きで
    // 呼ぶため、後退中もheadは「今まさに先進している側の軌道位置」として扱われ、
    // trailingPasses(ポイントの割り出し)は前進時と同じくそのまま適用してよい
    // (rs::world::ChaseBackwardAcrossScenesがtrailingPassesを常に空にするのは、
    // 後方chase=編成内の依存する軸を辿る用途で、その軸は先進していないため。
    // ここは編成そのものの先頭headの移動なので同じ扱いはできない)。
    rs::world::TrackPosition advanceStart = kin.head;
    const bool reversing = ds < 0.0;
    running.lastTravelReversed = reversing;
    if (reversing) advanceStart.direction = -advanceStart.direction;
    rs::world::PointConditionContext conditionContext;
    conditionContext.trainId = trainId;
    conditionContext.formationId = formationId;
    conditionContext.trainTagIds = &trainTagIds;
    conditionContext.previousOccupiedBlocks = &previousOccupiedBlocks;
    conditionContext.nowAbsSeconds = nowAbsSeconds;
    const LeadingEnd leadingEnd = ResolveLeadingEnd(
        constGraphForScene, warpTable, running.sceneIndex, running.kin.head, reversing,
        running.occupiedPath, running.occupiedPathKeepLengthM);
    rs::world::ApplyPointRoutinesAheadAcrossScenes(
        graphForScene, warpTable, leadingEnd.sceneIndex, leadingEnd.position, reversing ? -ds : ds,
        pointRoutineCursor_, occupiedBlocks, conditionContext);
    rs::world::WarpAdvanceResult result = rs::world::AdvanceAcrossScenes(
        graphForScene, warpTable, running.sceneIndex, advanceStart, reversing ? -ds : ds);
    if (reversing) result.pos.direction = -result.pos.direction;
    kin.head = result.pos;
    kin.atEnd = result.atEnd;
    running.sceneIndex = result.sceneIndex;
    if (result.atEnd) kin.speed = 0.0;

    UpdateOccupiedPathAfterAdvance(running, result, reversing, constGraphForScene, warpTable);
    WriteBackTrailingPasses(graphForScene, result);

    // result.consumedは常に非負(Advance/AdvanceAcrossScenesの約束事)なので、後退時は
    // ここで符号を戻す(outDsMetersはSolveTrainVehicleへそのまま渡り、車輪回転方向の
    // 符号として使われる)。
    outDsMeters = reversing ? -result.consumed : result.consumed;
    running.odometerM += outDsMeters;
    outHead = kin.head;
    outSceneIndex = running.sceneIndex;
    return true;
}

} // namespace rs::sim

//
// FormationRoutineResolve.h - 編成別ルーチンの旧データ移行と配置元登録。
//
#pragma once

#include <cstddef>
#include <functional>

#include "Guid.h"
#include "../scene/TrainId.h"

namespace rs::world {

struct TrackGraph;
class TrainTagRegistry;

// 保存済みのTrainId別ルーチンを配置元の編成定義へ移す。同じ編成IDの既存層または
// 先に現れた列車の層を残し、旧TrainId別配列は空にする。戻り値は移行件数。
int MigrateTrainRoutinesToFormations(TrackGraph& graph, const TrainTagRegistry& tagRegistry);
int MigrateTrainRoutinesToFormations(
    const std::function<TrackGraph*(size_t)>& graphForScene, size_t sceneCount,
    const TrainTagRegistry& tagRegistry);

// 編成から列車を置いたときに配置元を登録する。ルーチン表は編成定義を共有し、
// 実行カーソルのみ列車ごとに保持する。
void RegisterPlacedTrainFormation(const Guid& formationId, TrainId trainId,
                                  TrainTagRegistry& tagRegistry);

}  // namespace rs::world

//
// PointRoutine.h - 分岐(TrackNode)の開通先ルーチン表。
//
#pragma once

#include <vector>

#include "Guid.h"
#include "PlatformRoutine.h"
#include "../scene/TrainId.h"

namespace rs::world {
struct TrackNode;

// ルーチン表の1要素。開通先は hand(左右、面に依存しない)を持つ。
struct PointRoutineElement {
    // 無効要素はPeek/Dequeueでスキップし、カーソルを消費しない。
    bool enabled = true;
    // RailSim II のPointList.Point値そのもの(0=左/1=右)。面に依存しない生の値。実際に
    // どちらのリンクへ解決されるかは、通過する出口面の幾何で毎回決まる
    // (ResolveHandToOrdinal、TrackTraversal.h参照)。
    int hand = 0;
    // true なら hand を見ず、通過のたびに左右を乱数で決める(PointList.Point=0)。
    bool random = false;
};

// 条件1件。timeRange/blockOccupancyのどちらか一方の判定式
// のみ使う(kindで選択)。
struct PointCondition {
    enum class Kind { TimeRange, BlockOccupancy };
    bool enabled = true;              // PointRoutineElementと同じ無効化の仕組み
    Kind kind = Kind::TimeRange;
    DiaTimeOfDay startTime;           // TimeRange用。日跨ぎ(start > end)も許容する
    DiaTimeOfDay endTime;             // (23:00〜翌01:00等)
    std::vector<Guid> blocks;         // BlockOccupancy用。空なら常に不一致
    bool requireAllOccupied = false;  // true=すべて在線、false=いずれかが在線
    int hand = 0;                     // 一致したときの開通方向(0=左/1=右)。実際のスロットは出口面から解決する
};

struct PointTrainCondition {
    TrainId trainId = NilGuid();
    std::vector<PointCondition> conditions;
};

struct PointTagCondition {
    int tagId = -1;
    std::vector<PointCondition> conditions;
};

struct PointTrainRoutine {
    TrainId trainId = NilGuid();
    std::vector<PointRoutineElement> routine;
};

// 編成定義(Formation)別のポイントルーチン。配置前の編成に紐付くため、TrainIdではなく
// Formation guidをキーに持つ。編成が配置された時点でPointTrainRoutineへ複写される
// (FormationRoutineResolve.h、ResolveFormationRoutinesForTrain)。
struct PointFormationRoutine {
    Guid formationId = NilGuid();
    std::vector<PointRoutineElement> routine;
};

struct PointTagRoutine {
    int tagId = -1;
    std::vector<PointRoutineElement> routine;
};

struct ResolvedPointRoutine {
    const std::vector<PointRoutineElement>* routine = nullptr;
    int layerKey = kCommonRoutineLayerKey;
    TrainId trainId = NilGuid();
};

ResolvedPointRoutine ResolvePointTrainRoutine(
    const std::vector<PointTrainRoutine>& trainRoutines,
    const std::vector<PointTagRoutine>& tagRoutines,
    const std::vector<PointRoutineElement>& commonRoutine, TrainId trainId,
    const std::vector<int>& trainTagIds);

ResolvedPointRoutine ResolvePointTrainRoutine(
    const std::vector<PointTrainRoutine>& trainRoutines,
    const std::vector<PointFormationRoutine>& formationRoutines,
    const std::vector<PointTagRoutine>& tagRoutines,
    const std::vector<PointRoutineElement>& commonRoutine, TrainId trainId,
    const Guid& formationId, const std::vector<int>& trainTagIds);

// ResolveTrainRoutine(PlatformRoutine.h)と同じ優先順位(個別>タグ>共通)で、
// trainId/trainTagIdsに適用される条件リストを1つ選んで返す。該当が無くても
// 常にcommonConditionsを指す非nullptrを返す(最後の手段)。
const std::vector<PointCondition>* ResolvePointConditionLayer(
    const std::vector<PointTrainCondition>& trainConditions,
    const std::vector<PointTagCondition>& tagConditions,
    const std::vector<PointCondition>& commonConditions,
    TrainId trainId, const std::vector<int>& trainTagIds);

// nowAbsSecondsの時刻部分(std::fmod(nowAbsSeconds, 86400.0)、負値は86400を
// 足して補正)がstart〜endの範囲に入っているか判定する。start<=endなら
// [start,end)の通常範囲、start>endなら日跨ぎとして[start,86400)∪[0,end)の範囲。
bool TimeOfDayInRange(double nowAbsSeconds, const DiaTimeOfDay& start, const DiaTimeOfDay& end);

// 無効要素をスキップして現在カーソルの要素を返す。空/全無効ならnullptr。
const PointRoutineElement* PeekPointRoutineElement(
    const std::vector<PointRoutineElement>& routine, int cursor);

// Peekと同じ要素を返し、その有効要素の次へカーソルを進める。
const PointRoutineElement* DequeuePointRoutineElement(
    const std::vector<PointRoutineElement>& routine, int& cursor);

}  // namespace rs::world

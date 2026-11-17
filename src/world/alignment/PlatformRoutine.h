//
// PlatformRoutine.h - 番線ダイヤ設定(ルーチン表)の型とDequeue/Peek。
// ダイヤの「全列車共通」層のみを表す
// (個別編成/タグ層は別データとして追加する。この構造体には混ぜない)。
//
// ラウンドロビンの「回転」はここでは行わない。PlatformStopInfo::routine自体は
// TrackGraph内のUndo対象データであり、編成が通過するたびに書き換えるとUndoの
// 整合が壊れる。代わりに、呼び出し側(rs::sim::DiaRuntime)が番線Guid単位で
// 持つ実行時カーソル(int, Undo対象外)をDequeueRoutineElementで前進させる形で
// 同じ意味づけを実現する。
//
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "../scene/TrainId.h"

namespace rs::world {

struct TrainRunInfo;

// 駅での動作(DiaElement.Action)。挙動へ配線しているのはStopだけ
// (Reverse、Passは配線していない)。
enum class DiaAction {
    Stop = 0,
    Reverse = 1,
    Pass = 2,
};

// 動作の名前('stop' / 'reverse' / 'pass')。保存と画面で同じ名前を使う。
const char* DiaActionKey(DiaAction action);
// 名前から動作を引く。知らない名前ならnullopt。
std::optional<DiaAction> DiaActionFromKey(std::string_view key);

// 発車条件の種別(DiaElement.TimeType)。
enum class DiaTimeType {
    ElapsedFromArrival = 0,  // 到着からの経過時間で発車
    AbsoluteTimeOfDay = 1,   // 日内時刻を指定して発車(複数可)
};

// 日内時刻。0<=hour, 0<=minute<60, 0<=second<60を前提とする(正規化はしない)。
struct DiaTimeOfDay {
    int hour = 0;
    int minute = 0;
    int second = 0;
};

struct SwitchChange {
    std::string switchName;
    int32_t value = 0;
};

struct RunInfoChange {
    // 運行情報の書換。エンジン算出・表示専用のフィールドは対象外。
    enum class Field {
        Type,
        Destination,
        LineName,
        TrainNumber,
        DutyNumber,
    };
    Field field = Field::Type;
    std::string value;
};

// ルーチン表の1要素(DiaElement相当)の「全列車共通」層。
struct PlatformRoutineElement {
    // 無効化された要素。Dequeue/Peekはこれをスキップし、スキップはカーソルを
    // 進めない(次にこの番線へ来た編成が改めてこの要素を評価し直す)。
    bool enabled = true;

    DiaAction action = DiaAction::Stop;

    DiaTimeType timeType = DiaTimeType::ElapsedFromArrival;
    double dwellSeconds = 30.0;  // ElapsedFromArrival用

    // AbsoluteTimeOfDay用。到着した瞬間のゲーム内時刻以降で最初に来る1つを採用する
    // (全て過ぎていれば翌日)。リストは消費しない(何度でも同じ要素を使い回せる)。
    std::vector<DiaTimeOfDay> departureTimes;

    // 要素単位の停止位置(正規化[-1,+1]、-1=番線の手前端 / 0=中央 / +1=奥端)。
    // 未設定なら番線既定(PlatformStopInfo::stopReference/stopOffsetM)を使う。
    std::optional<double> stopOffsetNormalized;
    std::vector<SwitchChange> switchChanges;
    // 指定されたフィールドだけを到着時に書き換える。
    std::vector<RunInfoChange> runInfoChanges;
};

struct PlatformTrainRoutine {
    TrainId trainId = NilGuid();
    std::vector<PlatformRoutineElement> routine;
};

struct PlatformTagRoutine {
    int tagId = -1;
    std::vector<PlatformRoutineElement> routine;
};

// 編成定義(Formation)別の停車ルーチン。配置前の編成に紐付くため、TrainIdではなく
// Formation guidをキーに持つ。編成が配置された時点でPlatformTrainRoutineへ複写される
// (FormationRoutineResolve.h、ResolveFormationRoutinesForTrain)。
struct PlatformFormationRoutine {
    Guid formationId = NilGuid();
    std::vector<PlatformRoutineElement> routine;
};

inline constexpr int kCommonRoutineLayerKey = -1;
// Tag layers use -2 and below, avoiding the common layer (-1) and train layers (>= 0).
inline int TagRoutineLayerKey(int tagId) { return -(tagId + 2); }

struct ResolvedPlatformRoutine {
    const std::vector<PlatformRoutineElement>* routine = nullptr;
    int layerKey = kCommonRoutineLayerKey;
    TrainId trainId = NilGuid();
};

ResolvedPlatformRoutine ResolveTrainRoutine(
    const std::vector<PlatformTrainRoutine>& trainRoutines,
    const std::vector<PlatformTagRoutine>& tagRoutines,
    const std::vector<PlatformRoutineElement>& commonRoutine, TrainId trainId,
    const std::vector<int>& trainTagIds);

ResolvedPlatformRoutine ResolveTrainRoutine(
    const std::vector<PlatformTrainRoutine>& trainRoutines,
    const std::vector<PlatformFormationRoutine>& formationRoutines,
    const std::vector<PlatformTagRoutine>& tagRoutines,
    const std::vector<PlatformRoutineElement>& commonRoutine, TrainId trainId,
    const Guid& formationId, const std::vector<int>& trainTagIds);

// routineの中から、cursorが指す位置以降で最初に見つかる有効要素のインデックスを返す。
// 全要素が無効/空なら-1を返す。cursorは範囲外の値でも内部で正規化する。
int ResolveNextRoutineIndex(const std::vector<PlatformRoutineElement>& routine, int cursor);

// routineの中から、cursorが指す位置以降で最初に見つかる有効要素を返す(非破壊)。
// 全要素が無効/空ならnullptrを返す。cursorは範囲外の値でも内部で正規化する。
const PlatformRoutineElement* PeekRoutineElement(
    const std::vector<PlatformRoutineElement>& routine, int cursor);

// PeekRoutineElementと同じ要素を返しつつ、cursorをその要素の次の位置へ進める。
// 有効要素が1つも無ければcursorは変更せずnullptrを返す。
const PlatformRoutineElement* DequeueRoutineElement(
    const std::vector<PlatformRoutineElement>& routine, int& cursor);

// 指定されたフィールドだけを上書きし、それ以外は維持する。
TrainRunInfo ApplyRunInfoChanges(const TrainRunInfo& base,
                                   const std::vector<RunInfoChange>& changes);

}  // namespace rs::world

//
// DiaRuntime.h - 編成(trainId)ごとの運行状態機械(停車→ドア扱い→発車時刻待ち→発車)。
// RailSim II の CTrainGroup::m_State 0/1/2/3 に対応する。
//
// 「停車中か」「いつ発車するか」の唯一の情報源はこのクラスにする。
// TrainRuntime::GroupArrivedAtStop(キネマティクス上の「停止目標に到達して速度0に
// なった」というbool)は時間軸を持たないので、ドア開閉・接近判定の`_STOPPING`・UI表示は
// これを個別に解釈せず、ここの状態を読む。
//
// 設計上の要点:
//  - 発車判定は絶対ゲーム内秒(rs::world::GameClock::AbsoluteSeconds)どうしの比較で
//    行い、残り秒のカウントダウンやtick数え上げは使わない。停車時間がTimeScaleや
//    tick粒度で変わらないようにするため。
//  - GameClockではなくdouble nowAbsSecondsを受け取る。rs::app層(固定tickループ)へ
//    依存させず、テストから任意の時刻を差し込めるようにするため。
//  - 停車サイクルを始めるのは「番線への到着」に限る。FormationPanelの手動停止距離指定の
//    ような番線を伴わない停止では、Stoppedを報告するが発車予定は組まない(勝手に
//    走り出さない)。
//  - どの番線に停まっているか(visitPlatform)もここが持つ。TrainRuntimeの
//    targetPlatformは一度検出した番線を、別の番線を検出するまで保持し続けるラッチで、
//    「発車済み」を表現できないため。
//
// 停車時間はSetGroupStopSpecで外から差し込む形にしてある。ルーチン表を持つ
// 呼び出し側が、Dequeue結果をこの経路でここへ流し込む。
//
#pragma once

#include <map>
#include <unordered_map>
#include <vector>

#include "../world/alignment/TrackGraph.h"
#include "../world/scene/TrainRunInfoRegistry.h"
#include "../world/scene/TrainTagRegistry.h"
#include "../world/scene/SceneEntity.h"
#include "SimTuning.h"

namespace rs::sim {

using rs::world::TrainId;

// ダイヤ未設定の駅での既定の停車時間[ゲーム内秒]。
// ルーチン表が無い間は全編成・全番線がこの既定値で動く。

// RailSim II の CTrainGroup::m_State と同じ意味づけ(Rs2Types.h の Rs2TrainGroupRunState::state)。
enum class DiaState {
    Running = 0,    // 走行中(停止目標なし)
    Braking = 1,    // 停止目標へ減速中
    Stopped = 2,    // 停車中(ドアを開けて発車時刻を待っている)
    Departing = 3,  // 発車直前(ドアを閉じ終えるのを待っている)
};

// 1回の停車の設定。ルーチン要素(DiaElement)から作られない場合は、
// 既定値のまま使うか呼び出し側が明示的に差し込む。
struct DiaStopSpec {
    enum class TimeType {
        ElapsedFromArrival,  // 到着からの経過時間で発車(.rs2 TimeType=0)
        AbsoluteTimeOfDay,   // 日内時刻を指定して発車(.rs2 TimeType=1)
    };
    TimeType timeType = TimeType::ElapsedFromArrival;
    double   dwellSeconds = SimTuning::Embedded().dia.defaultDwellSeconds;  // ElapsedFromArrival用
    std::vector<rs::world::DiaTimeOfDay> departureTimes;
    // 駅での動作(DiaElement.Action)。既定Stopは通常の停車発車。
    // Reverseは発車時に目標速度の符号を反転させる合図として使う。
    rs::world::DiaAction action = rs::world::DiaAction::Stop;
    std::vector<rs::world::SwitchChange> switchChanges;
    std::vector<rs::world::RunInfoChange> runInfoChanges;
};

// 毎tickの入力。呼び出し側(rs::app::SceneTrainController)がTrainRuntimeから作る。
struct DiaTickInput {
    bool            hasStopTarget = false;  // TrainRuntime::GroupHasStopTarget
    bool            arrivedAtStop = false;  // TrainRuntime::GroupArrivedAtStop
    rs::world::Guid targetPlatform = rs::world::NilGuid();  // TrainRuntime::GroupTargetPlatform
    const rs::world::PlatformStopInfo* targetPlatformInfo = nullptr;
    // 発車の何ゲーム内秒前にドアを閉じ始めるか(DiaRuntime::DoorCloseLeadSeconds)。
    // 到着したtickの値だけが使われ、停車中に変わっても発車予定は動かない。
    double doorCloseLeadSeconds = 0.0;
};

struct DiaTickResult {
    DiaState state = DiaState::Running;
    // ドアを開けてよいか(停車中のみtrue。発車直前=Departingでは閉じ始めるためfalse)。
    bool doorsOpen = false;
    std::vector<rs::world::SwitchChange> activeSwitchChanges;
    // このtickで発車した。呼び出し側はTrainRuntime::ClearGroupStopTargetを呼ぶこと
    // (自動停止目標を外すだけで、編成は元のtargetSpeedどおりに再加速する)。
    bool departNow = false;
    // このtickで到着時のRunInfoChangeを適用したか。駅/番線プラグインへの反映トリガに使う。
    bool runInfoJustApplied = false;
    // departNow==trueのときのみ意味を持つ、この停車で適用されたAction。
    // Reverseなら、呼び出し側はClearGroupStopTargetに加えて
    // TrainRuntime::SetGroupTargetSpeedで目標速度の符号を反転させること。
    rs::world::DiaAction departAction = rs::world::DiaAction::Stop;
};

struct DiaRuntimeGroupSnapshot {
    TrainId trainId = rs::world::NilGuid();
    DiaState state = DiaState::Running;
    rs::world::Guid visitPlatform = rs::world::NilGuid();
    rs::world::Guid departedPlatform = rs::world::NilGuid();
    bool departureScheduled = false;
    double departureAbsSeconds = 0.0;
    double doorCloseAbsSeconds = 0.0;
    double phaseElapsed = 0.0;
    rs::world::DiaAction currentAction = rs::world::DiaAction::Stop;
    std::vector<rs::world::SwitchChange> currentSwitchChanges;
};

struct DiaRuntimeSnapshot {
    std::vector<DiaRuntimeGroupSnapshot> groups;
    std::unordered_map<rs::world::Guid, std::unordered_map<int, int>, rs::world::GuidHash>
        routineCursors;
    std::unordered_map<rs::world::Guid,
                       std::unordered_map<TrainId, int, rs::world::GuidHash>,
                       rs::world::GuidHash> trainRoutineCursors;
};

// 1回の停車の発車予定(絶対ゲーム内秒)。
struct DiaDepartureSchedule {
    double departureAbsSeconds = 0.0;
    double doorCloseAbsSeconds = 0.0;
};

class DiaRuntime {
public:
    // 到着時刻から発車予定とドア閉じ開始時刻を求める。到着の処理と、停車中の状態を
    // 外から作る処理(MakeArrivedSnapshot)が同じ計算を使うため、ここ1か所に置く。
    // doorCloseLeadSecondsは発車の何ゲーム内秒前にドアを閉じ始めるか。発車時刻より前に
    // 来るようクランプされる。
    static DiaDepartureSchedule ScheduleDeparture(const DiaStopSpec& spec,
                                                  double arrivalAbsSeconds,
                                                  double doorCloseLeadSeconds);
    // ドアを閉じ始める先行時間[ゲーム内秒]。RailSim II は到着時に、番線のOpenDoorが立っていれば
    // 編成の最大のDoorClosingTime(フレームへ丸める)、立っていなければ0を待ち時間にする。
    // doorClosingTimeSecondsは編成内の車両のDoorClosingTimeの最大値[実秒]、
    // timeScaleは到着した時点の値。
    static double DoorCloseLeadSeconds(double doorClosingTimeSeconds, bool doorsOpen,
                                       double timeScale);
    // ルーチン要素の内容をbaseに重ねた停車設定を返す。
    static DiaStopSpec ApplyRoutineElement(DiaStopSpec base,
                                           const rs::world::PlatformRoutineElement& element);
    // すでに番線へ到着して停車中(Stopped)または発車直前(Departing)の編成を表す
    // スナップショットを作る。nowAbsSecondsを到着時刻として扱い、ルーチン要素の
    // Dequeueは通さない(呼び出し側がすでに取り出した要素をspecに渡す)。
    // currentActionは常にStop。折り返しはすでに目標速度の符号へ反映済みの前提。
    // stateがStopped/Departing以外ならStoppedとして扱う。
    static DiaRuntimeGroupSnapshot MakeArrivedSnapshot(TrainId trainId, DiaState state,
                                                       const rs::world::Guid& platform,
                                                       const DiaStopSpec& spec,
                                                       double nowAbsSeconds,
                                                       double doorCloseLeadSeconds);

    // 次に始まる停車の設定を差し込む(以後この編成の全停車に適用される)。
    // ルーチン表が入ったら、その1要素ぶんをここへ流し込む形になる。
    void SetGroupStopSpec(TrainId trainId, const DiaStopSpec& spec);

    // 状態機械を1tick進める。nowAbsSecondsはGameClock::AbsoluteSeconds()の値。
    // 状態遷移は同一tick内で連鎖させる(Stopped→Departing→Running)。高TimeScale時は
    // 1tickのゲーム内経過がドア閉じ時間を丸ごと超えうるため、else ifで分岐すると
    // 発車が1tick遅れて停車時間がTimeScale依存になる。
    DiaTickResult Tick(TrainId trainId, const DiaTickInput& input, double nowAbsSeconds);

    DiaState GroupState(TrainId trainId) const;
    // 今停車中の番線(走行中/手動停止ならNilGuid)。ドア開閉のOpenDoor定義の引き当てに使う。
    rs::world::Guid GroupVisitPlatform(TrainId trainId) const;
    // 発車予定の絶対ゲーム内秒。発車予定が組まれていなければfalseを返す(UI表示用)。
    bool GroupDepartureAbsSeconds(TrainId trainId, double& outAbsSeconds) const;
    // 次にこの番線へ来る列車が消費するルーチン要素の解決済みインデックスを返す(UI表示用)。
    // 未訪問の番線はcursor=0として扱い、routineが空/全disabledなら-1を返す。
    int PlatformRoutineCursorIndex(
        const rs::world::Guid& platformId,
        const std::vector<rs::world::PlatformRoutineElement>& routine,
        int layerKey = rs::world::kCommonRoutineLayerKey,
        TrainId trainId = rs::world::NilGuid()) const;

    // 接近中の番線について、次に適用される予定のルーチン要素を返す(カーソルを進めない)。
    // 解決層(個別/タグ/共通)はResolveTrainRoutineに従う。要素が無ければnullptr。
    const rs::world::PlatformRoutineElement* PeekPlatformRoutine(
        const rs::world::PlatformStopInfo& platform, TrainId trainId) const;
    void ConsumePlatformRoutine(const rs::world::PlatformStopInfo& platform, TrainId trainId);

    rs::world::TrainTagRegistry& TagRegistry() { return tagRegistry_; }
    const rs::world::TrainTagRegistry& TagRegistry() const { return tagRegistry_; }
    rs::world::TrainRunInfoRegistry& RunInfoRegistry() { return runInfoRegistry_; }
    const rs::world::TrainRunInfoRegistry& RunInfoRegistry() const { return runInfoRegistry_; }

    void RemoveGroup(TrainId trainId);
    DiaRuntimeSnapshot Snapshot() const;
    void Restore(const DiaRuntimeSnapshot& snapshot);
    void Clear();

private:
    struct GroupRun {
        DiaState        state = DiaState::Running;
        DiaStopSpec     spec;
        rs::world::Guid visitPlatform = rs::world::NilGuid();
        // 直前に発車した番線。TrainRuntimeのtargetPlatformラッチが残ったまま
        // 手動停止された場合に、同じ番線で停車サイクルを二重に始めないためのガード。
        rs::world::Guid departedPlatform = rs::world::NilGuid();
        bool            departureScheduled = false;
        double          departureAbsSeconds = 0.0;
        double          doorCloseAbsSeconds = 0.0;
        double          phaseElapsed = 0.0;
        double          lastPhaseSampleAbsSeconds = 0.0;
        // この停車1回に適用されたAction。発車tickでDiaTickResult::departActionへ渡す。
        rs::world::DiaAction currentAction = rs::world::DiaAction::Stop;
        std::vector<rs::world::SwitchChange> currentSwitchChanges;
    };
    std::map<TrainId, GroupRun> groups_;
    rs::world::TrainTagRegistry tagRegistry_;
    rs::world::TrainRunInfoRegistry runInfoRegistry_;
    std::unordered_map<rs::world::Guid, std::unordered_map<int, int>, rs::world::GuidHash> routineCursor_;
    std::unordered_map<rs::world::Guid,
                       std::unordered_map<TrainId, int, rs::world::GuidHash>,
                       rs::world::GuidHash> trainRoutineCursor_;
    rs::world::ResolvedPlatformRoutine ResolvePlatformRoutine(
        const rs::world::PlatformStopInfo& platform, TrainId trainId) const;
    int ReadPlatformRoutineCursor(const rs::world::PlatformStopInfo& platform,
                                  const rs::world::ResolvedPlatformRoutine& resolved) const;
    int& WritePlatformRoutineCursor(const rs::world::PlatformStopInfo& platform,
                                    const rs::world::ResolvedPlatformRoutine& resolved);
};

} // namespace rs::sim

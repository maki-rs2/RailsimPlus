#pragma once

#include <functional>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "BlockOccupancy.h"
#include "../world/alignment/BlockSpeedLimit.h"
#include "../world/alignment/TrackGraph.h"
#include "../world/alignment/WarpLinkTable.h"

namespace rs::sim {

// 信号の段の速度列(段0から順に[km/h])を返す関数。SpeedSignalのデータ形に依存させないために
// 呼び出し側が渡す。段0は0(赤)で、段が上がるほど速度が大きい(IsValidAspectTable)。
using SignalStageSpeedsFn = std::function<std::vector<int>(const rs::world::SpeedSignal&)>;

// 信号のstagesから速度列(段0から順に[km/h])を作る。SignalStageSpeedsFnにそのまま使える。
std::vector<int> SignalStageSpeeds(const rs::world::SpeedSignal& signal);

// 段の速度列の不変条件: 空でない・先頭が0・狭義単調増加。段表のチェックはここ1か所に置く。
bool IsValidAspectTable(const std::vector<int>& speeds);

// 段を選ぶ純関数。在線または強制Rなら0。前方の信号の段の速度が無ければ最上段。あれば、
// 前方の速度以下の最大の段の1つ上(最上段で頭打ち)。
int ComputeSignalAspectIndex(const std::vector<int>& speeds, bool blockOccupiedOrForcedRed,
                             std::optional<int> forwardAspectSpeedKmh);

// 列車trainIdに適用される段。強制Rまたは進路が開通していなければ0、担当閉塞にtrainId以外の列車がいれば0、
// そうでなければclearStage。
// 自分の在線で止まらないための規則(自分が入った閉塞の信号で自分が止まるのを防ぐ)。
// trainId以外の在線の判定は BlocksOccupiedByOtherTrains(BlockOccupancy.h)と同じ。
int StageForTrain(int clearStage, bool forcedRed, bool routeDenied,
                  const std::vector<TrainId>& occupants, TrainId trainId);

// 信号1本の現示。毎tick計算し直す値で、保存もUndoの対象にもしない。
struct SignalAspectState {
    int clearStage = 0;    // 担当閉塞が空で強制Rも無いときの段
    int displayStage = 0;  // 在線・強制R・進路が開通していないなら0、そうでなければclearStage
    // 前方の信号(無ければ終端)までに、合流で入るポイントが入ってくる線に開通していない。
    // 在線や強制Rとは別の赤で、進入許可では通れない。
    bool routeDenied = false;
    std::vector<TrainId> occupants;  // 担当閉塞の在線列車
};

class SignalAspectRuntime {
public:
    // 手動の強制R。実行時だけの状態で、保存しない。すでに閉塞にいる列車にも適用される。
    void SetForceRed(const rs::world::Guid& signalGuid, bool forced);
    bool IsForcedRed(const rs::world::Guid& signalGuid) const;

    // 手動の進入許可。実行時だけの状態で、保存しない。入っている信号では、在線または強制Rの赤
    // (段0)の制限速度を、0ではなく調整値(SimTuning::signalMargins.entryPermitSpeedKmh)にする。
    // 表示の段は変えない。閉塞にいる間も有効。
    void SetEntryPermit(const rs::world::Guid& signalGuid, bool permitted);
    bool IsEntryPermitted(const rs::world::Guid& signalGuid) const;

    // 手動の強制R・進入許可と、計算済みの現示を全部捨てる(別のレイアウトへ移るとき)。
    void Clear();

    // 全信号の現示を計算し直す(pull)。担当閉塞を持たない信号と、段表が不正な信号は
    // 計算に入れない(Findがnullptrを返し、前方の信号としても数えない)。
    // シーン1枚・ワープなしの下の版として動く。
    void Recompute(const rs::world::TrackGraph& graph, const BlockOccupancySnapshot& occupancy,
                   const SignalStageSpeedsFn& stageSpeeds);
    // 複数シーン版。シーン0からsceneCount-1(graphForSceneがnullptrを返すシーンで打ち切る)の信号を
    // まとめて計算し、状態は全シーン分を持つ。occupancyは全シーン統合済み(キーが閉塞guid)。
    // 前方の信号の探索はwarpTableのシーン間ワープを跨ぐ(ポイントは共有のactiveSlotの開通先)。
    // 信号のguidはシーンをまたいで一意とみなす。
    void Recompute(const std::function<const rs::world::TrackGraph*(size_t)>& graphForScene,
                   size_t sceneCount, const rs::world::WarpLinkTable& warpTable,
                   const BlockOccupancySnapshot& occupancy, const SignalStageSpeedsFn& stageSpeeds);

    const SignalAspectState* Find(const rs::world::Guid& signalGuid) const;

    // 列車trainIdに適用される段。信号が計算に入っていなければnullopt。
    std::optional<int> StageForTrainAt(const rs::world::Guid& signalGuid, TrainId trainId) const;

private:
    std::unordered_map<rs::world::Guid, SignalAspectState, rs::world::GuidHash> states_;
    std::unordered_set<rs::world::Guid, rs::world::GuidHash> manualForcedRed_;
    std::unordered_set<rs::world::Guid, rs::world::GuidHash> manualEntryPermit_;
};

// 列車trainIdに適用される信号の制限速度を返す関数(EffectiveSpeedLimitsなどへ渡す)。
// runtimeの現示(StageForTrainAt)で段を引き、stages[段].speedKmhを返す。段0で進入許可が入っていれば
// 調整値の速度を返す。状態が無い信号
// (担当閉塞なし・段表が不正)や段が範囲外ならnullopt。runtimeは関数より長く生きること。
// deniedSignalsは、この列車がまだ進路(ポイントの予約)を取れていない信号。含まれる信号は停止(0)にする。
rs::world::SignalSpeedForTrainFn MakeSignalSpeedForTrainFn(
    const SignalAspectRuntime& runtime, TrainId trainId,
    const std::unordered_set<rs::world::Guid, rs::world::GuidHash>* deniedSignals = nullptr);

// 信号を表示するときの現示。パネルの文言とビューポートの色が同じ値を使うように、引く処理をここに1つ置く。
struct SignalDisplay {
    std::optional<int> stage;     // 現示の段。状態が無い(未計算・計算対象外)ときはnullopt
    std::optional<int> speedKmh;  // 表示する速度。状態があれば段の速度、無ければ最上段。stagesが空ならnullopt
};

// runtimeにこの信号の状態があり、displayStageがstagesの範囲内なら、その段と速度を返す。
// そうでなければ最上段の速度(SignalTopSpeedKmh)だけを返す。runtimeがnullptrなら状態なしとして扱う。
SignalDisplay ResolveSignalDisplay(const SignalAspectRuntime* runtime, const rs::world::SpeedSignal& signal);

} // namespace rs::sim

//
// PointRoutineResolve.h - 分岐を物理的に通過する直前に開通先を確定する。
//
#pragma once

#include <functional>
#include <random>
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <vector>

#include "TrackTraversal.h"
#include "WarpForwardScan.h"
#include "WarpLinkTable.h"
#include "../scene/TrainId.h"
#include "Guid.h"

namespace rs::world {

struct TrackGraph;
struct TrackNode;

// ポイント在線ガード判定。nodeにつながるedgeのどれかがoccupiedBlocks(他編成が
// 在線している閉塞)に属していれば真。ガードの判定はこの関数1か所に集める。
bool IsNodeAdjacentToOccupiedBlock(const TrackGraph& graph, const TrackNode& node,
                                   const std::unordered_set<Guid, GuidHash>& occupiedBlocks);

// 前方spanが通る分岐のうち、在線ガードが掛かっていて、かつspanが進む先(=転換されていない
// 古い開通の先)のedgeが他編成の在線閉塞に属する最初の分岐までの距離[m]を返す。
// 無ければnullopt。spanは列車ごとの先読みの決定付きで作られ、ガード中の分岐は決定に
// 入らないため、activeSlot(古い開通)をたどっている。ルーチンの中身は見ない。
std::optional<double> FindGuardedPointStopAheadM(
    const std::function<const TrackGraph*(size_t)>& graphForScene, const ForwardScanSpan& span,
    const std::unordered_set<Guid, GuidHash>& occupiedBlocks);

// 条件レイヤ評価に必要な文脈。呼び出し側(TrainRuntime.cpp)が組み立てる。
struct PointConditionContext {
    TrainId trainId = NilGuid();
    const std::vector<int>* trainTagIds = nullptr;
    const std::unordered_set<Guid, GuidHash>* previousOccupiedBlocks = nullptr;
    double nowAbsSeconds = 0.0;
    Guid formationId = NilGuid();
};

// ポイントルーチンのカーソル保持。DiaRuntimeのroutineCursor_/trainRoutineCursor_と
// 同じ二本立て(層別/編成別)。編成別ルーチンを共有カーソルで消費すると他編成の順番が
// 進むため分ける。
struct PointRoutineCursors {
    std::unordered_map<Guid, std::unordered_map<int, int>, GuidHash> layerCursors;
    std::unordered_map<Guid, std::unordered_map<TrainId, int, GuidHash>, GuidHash> trainCursors;

    // 直近のspan構築(ResolveSeekAheadPointRoutines)で「編成が到達済みとして
    // 開通先を確定した」分岐node/面ごとの決定(共有activeSlotへは書き込まない、
    // 列車ごとの決定)。編成ごとに保持する。次回のspan構築時、この中に既に決定が
    // あるnode/面は再Dequeueせず、その決定値をそのまま使う(=RailSim II のAddSeekが
    // 「前回passのseek集合に含まれていたか」を返すのと同じ規則)。読み取り専用の
    // 前方探索(TrackForwardScan/WarpForwardScan)へslotOverrideとして渡すことで、
    // 物理到達前でも先読み結果に一貫した経路を見せる。
    std::unordered_map<TrainId, NodeSlotOverride, GuidHash> seekDecisions;

    // 在線ガードが掛かっている間、先読みで決めた開通先を預かっておく場所。ガード中の
    // 分岐はseekDecisionsへ入れない(入れると先読みspanは新しい開通をたどるのに、実際の
    // 走行はactiveSlot=古い開通をたどってずれる)。ガードが外れたとき、ここの決定を
    // そのまま使い、ルーチンのカーソルを消費し直さない。
    std::unordered_map<TrainId, NodeSlotOverride, GuidHash> heldDecisions;

    // ランダム要素(PointRoutineElement::random)の左右を決める乱数。シード固定で、
    // Clear()で初期状態へ戻るため同じ運行は再現できる。
    std::mt19937_64 rng{kRandomSeed};
    static constexpr std::mt19937_64::result_type kRandomSeed = 20260509;

    void Clear();
    void RemoveTrain(TrainId trainId);
};

// trainId/formationId/trainTagIdsで層を解決し(ResolvePointConditionLayer)、その中の
// 条件を配列順に評価して最初に一致(enabled==trueかつ判定式が真)したものの`hand`を
// 返す。一致が無ければnullopt。TimeRangeはTimeOfDayInRangeで判定する。
// BlockOccupancyはpreviousOccupiedBlocksとの重なりで判定する。
std::optional<int> ResolvePointConditionHand(
    const TrackNode& node, TrainId trainId, const std::vector<int>& trainTagIds,
    const std::unordered_set<Guid, GuidHash>& previousOccupiedBlocks,
    double nowAbsSeconds);
// 編成の層も見る版。formationIdがNilGuidなら編成の層は飛ばす。
std::optional<int> ResolvePointConditionHand(
    const TrackNode& node, TrainId trainId, const Guid& formationId,
    const std::vector<int>& trainTagIds,
    const std::unordered_set<Guid, GuidHash>& previousOccupiedBlocks,
    double nowAbsSeconds);

// startからdsだけ先読みし、物理的に通過するルーチン設定済み分岐のactiveSlotを
// 確定する。Advance/AdvanceAcrossScenesの直前に同じ(start, ds)で1回だけ呼ぶ。
// この単一Scene版はscene境界を跨いだ後の同tick内の分岐を対象にしない。
// conditionContextを省略した呼び出しは、条件レイヤを常に評価しない(空タグ・
// 空previousOccupiedBlocks・trainId=-1)既定文脈で呼ぶのと同じ。
void ApplyPointRoutinesAhead(TrackGraph& graph, const TrackPosition& start, double ds,
                             PointRoutineCursors& routineCursors,
                             const std::unordered_set<Guid, GuidHash>& occupiedBlocks);
void ApplyPointRoutinesAhead(TrackGraph& graph, const TrackPosition& start, double ds,
                             PointRoutineCursors& routineCursors,
                             const std::unordered_set<Guid, GuidHash>& occupiedBlocks,
                             const PointConditionContext& conditionContext);

// startScene/startからdsだけ先読みし、warp境界を越えたScene内の分岐ルーチン/条件レイヤも
// 進行順に適用する。各分岐でactiveSlotを書き換えてから次リンクを選択する。
void ApplyPointRoutinesAheadAcrossScenes(
    const std::function<TrackGraph*(size_t)>& graphForScene, const WarpLinkTable& warpTable,
    size_t startScene, const TrackPosition& start, double ds,
    PointRoutineCursors& routineCursors,
    const std::unordered_set<Guid, GuidHash>& occupiedBlocks,
    const PointConditionContext& conditionContext);

// startScene/startからdsだけ先読みし(遠方span相当)、通過する分岐のうち
// routineCursors.seekDecisions[trainId]に既に決定が無いもの(=この編成が今回の接近で
// まだ到達していない分岐)だけ、ApplyPointRoutinesAheadAcrossScenesと同じロジックで
// 開通先を解決する。既にroutineCursors.seekDecisions[trainId]に決定がある分岐は
// 再解決せず(Dequeueしない)その決定値をそのまま通す。在線ガード中の分岐は
// カーソルを消費せず、前回までの決定(seekDecisions、なければheldDecisions)を
// heldDecisions[trainId]へ預ける。ガードが外れたら、その決定を使う。**この関数は共有activeSlotへは
// 一切書き込まない**(先読みは経路をプレビューするだけで、実際の分岐転換は物理通過
// パス(ApplyPointRoutinesAheadAcrossScenes)が担う)。呼び出しの最後に、今回のwalkで
// 通過した分岐全ての決定でroutineCursors.seekDecisions[trainId]と
// heldDecisions[trainId]を丸ごと置き換える
// (前回pass分は破棄。RailSim II の「search passごとに集合を作り直す」規則と同じ)。
void ResolveSeekAheadPointRoutines(
    const std::function<TrackGraph*(size_t)>& graphForScene, const WarpLinkTable& warpTable,
    TrainId trainId, size_t startScene, const TrackPosition& start, double ds,
    PointRoutineCursors& routineCursors,
    const std::unordered_set<Guid, GuidHash>& occupiedBlocks,
    const PointConditionContext& conditionContext);

}  // namespace rs::world

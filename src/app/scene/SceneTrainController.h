#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>

#include "../../sim/TrainRuntime.h"
#include "../../sim/DiaRuntime.h"
#include "../../sim/PlatformApproach.h"
#include "../../sim/BlockOccupancy.h"
#include "../../sim/RouteReservation.h"
#include "../../world/alignment/WarpLinkTable.h"
#include "../../world/scene/SceneRegistry.h"
#include "../../world/time/GameClock.h"
#include "../../world/time/SolarPosition.h"
#include "ScenePickItem.h"
#include "TrainDoorSide.h"
#include "SceneSignalPhase.h"
#include "SceneSlotPool.h"
#include "SceneTrainGroupCollector.h"
#include "StationRunInfoDispatcher.h"
#include "SceneTrainBatching.h"
#include "SceneTrainWheelSound.h"
#include "StaticMeshCache.h"

namespace rs::app {

class App;

using rs::world::TrainId;

// 運転台視点で使う先頭/後尾どちらの
// 車両・カブかを決める純関数(RailSim IIのCTrainGroup::SetCabinView、CTrainGroup.cpp:
// 427-430と同じ考え方: travelReversedなら連結順末尾の車両を使い、その車両個別の
// reversedフラグとXORしてFront/Tailを選ぶ)。SceneTrainController::ResolveCabinPivotから
// 呼ばれる(GPU/Scene非依存でテスト可能にするため分離)。
// carReversedInOrder: 連結順(carOrder昇順)に並んだ各車両の個別反転フラグ。
// 空ならfalseを返す(outLeadIndex/outTailCabinは変更しない)。
bool SelectCabinLeadCar(const std::vector<bool>& carReversedInOrder, bool travelReversed,
                        size_t& outLeadIndex, bool& outTailCabin);

// registry上のtrainId編成からSelectCabinLeadCarで選んだ対象車両のSceneEntity::guidを返す
// (連結順(carOrder)への整列とSelectCabinLeadCar呼び出しをまとめた共有ヘルパー)。ResolveCabinPivotと_CABINVIEWシステムスイッチ付与(SceneTrainController::Tick)
// の両方から呼ばれる。該当編成のCarエンティティが
// 1つも無い場合はfalseを返す(outLeadCarEntityId/outTailCabinは変更しない)。
bool ResolveCabinLeadCarEntityId(const rs::world::SceneRegistry& registry, TrainId trainId,
                                 bool travelReversed, rs::world::Guid& outLeadCarEntityId,
                                 bool& outTailCabin);

// slotsの中から、entityIdの編成車両が入っている使用中のスロットを返す(無ければnullptr)。
// 運転台視点のピボット解決と、先頭車の位置の解決が共有する。
const SceneSlotPool::Slot* FindTrainCarSlot(const std::vector<SceneSlotPool::Slot>& slots,
                                            const rs::world::Guid& carEntityId);

// trainId編成の先頭車(ResolveCabinLeadCarEntityIdと同じ選定)の車体の上面中央のワールド座標を返す。
// 先頭車がGPU常駐スロットを持たない、または車体の箱がまだ更新されていない・描画対象でないときはfalse
// (outAnchorは変更しない)。カブのJointを持たない車両でも解決できる。
bool ResolveLeadCarAnchor(const rs::world::SceneRegistry& registry,
                          const std::vector<SceneSlotPool::Slot>& slots, TrainId trainId,
                          bool travelReversed, rs::core::Vec3& outAnchor);

class SceneTrainController {
public:
    struct TickStats {
        double searchMs = 0.0;
        double kinematicsMs = 0.0;
        double partWorldMs = 0.0;
        double runtimeMs = 0.0;
        double submitMs = 0.0;
        double batchMs = 0.0;
        double fallbackMs = 0.0;
    };

    // cabinViewTrainId: 運転台視点がアクティブに
    // 追従中の編成(非アクティブならrs::world::NilGuid())。_CABINVIEWシステムスイッチ
    // (RailSim IIのCModelSwitch.cpp:559/CTrainGroup.cpp:780)の付与対象車両を決めるために使う。
    void Tick(rs::world::SceneRegistry& registry,
              const std::function<rs::world::TrackGraph*(size_t)>& graphForScene,
              const rs::world::WarpLinkTable& warpTable, size_t activeSceneIndex, double dt,
              double nowAbsSeconds, std::vector<SceneSlotPool::Slot>& slots,
              const SceneSlotPool& slotPool, StaticMeshCache& staticMeshCache,
              TrainId cabinViewTrainId, double timeScale);
    // wheelFrameは車輪音の継目判定の入力、wheelEventsには鳴らす要求を追記する。
    void SubmitRenderSnapshot(App& app, const rs::world::TrackGraph& activeGraph,
                              const float eye[3], const rs::world::GameClock& clock,
                              const rs::world::SolarState& sun, double latitudeDeg,
                              double nightThresholdSin,
                              std::vector<SceneSlotPool::Slot>& slots, float viewerFovYRadians,
                              const WheelSoundFrame& wheelFrame,
                              std::vector<rs::sim::WheelSoundEvent>& wheelEvents);
    void ResetWarpFade(std::vector<SceneSlotPool::Slot>& slots);
    // Sceneの並べ替え(oldToNew[旧]=新)に合わせて、Scene番号を持つ走行状態を付け替える。
    // 先読みの結果は次のtickで作り直されるので捨てる。
    void RemapSceneIndices(const std::vector<size_t>& oldToNew);
    void CollectPickCandidates(const rs::world::SceneRegistry& registry,
                               const std::vector<SceneSlotPool::Slot>& slots,
                               std::vector<ScenePickItem>& out) const;
    bool TryGetPickCandidate(const rs::world::SceneRegistry& registry, const rs::world::Guid& entityId,
                             const std::vector<SceneSlotPool::Slot>& slots, int slotIndex,
                             ScenePickItem& out) const;
    void SetGroupTargetSpeed(TrainId trainId, double targetSpeedKmh);
    void SetGroupCurrentSpeed(TrainId trainId, double speedKmh);
    double GroupTargetSpeedKmh(TrainId trainId) const;
    double GroupCurrentSpeedKmh(TrainId trainId) const;
    void SetGroupStopDistance(TrainId trainId, double remainingMeters);
    void ClearGroupStopTarget(TrainId trainId);
    bool GroupArrivedAtStop(TrainId trainId) const;
    // 編成の運行状態(停車中か・発車直前か)の唯一の情報源。ドア開閉・接近判定の
    // `_STOPPING`・UI表示はこれを見る(GroupArrivedAtStopはキネマティクス上の
    // 「停止目標に到達した」でしかなく、時間軸を持たない)。
    rs::sim::DiaState GroupDiaState(TrainId trainId) const;
    bool GroupDepartureAbsSeconds(TrainId trainId, double& outAbsSeconds) const;
    int PlatformRoutineCursorIndex(
        const rs::world::Guid& platformId,
        const std::vector<rs::world::PlatformRoutineElement>& routine,
        int layerKey = rs::world::kCommonRoutineLayerKey,
        TrainId trainId = rs::world::NilGuid()) const;
    int AddTrainTag(const std::string& label);
    void RestoreTrainTag(rs::world::TrainTagDef tag);
    // 別のレイアウトへ移るとき、走行状態(走行・ダイヤ・タグ・運行情報・ポイントの予約・在線・信号の
    // 現示と手動操作)を全部捨てる。保存しない状態が次のレイアウトへ残らないよう、消す場所をここ1つにする。
    void ResetForNewLayout();
    void RemoveTrainTag(int tagId);
    const std::vector<rs::world::TrainTagDef>& TrainTagDefs() const;
    const std::vector<int>& TrainTagsOf(TrainId trainId) const;
    const std::unordered_map<TrainId, rs::world::TrainRunInfo, rs::world::GuidHash>& TrainRunInfoAssignments() const;
    void SetTrainRunInfo(TrainId trainId, rs::world::TrainRunInfo info);
    void RemoveGroup(TrainId trainId, rs::world::SceneRegistry& registry);
    bool WouldTrainOverlap(const rs::world::SceneRegistry& registry,
                             const rs::world::TrackGraph& graph,
                             const rs::world::TrackPosition& candidateHead,
                             double candidateLengthM,
                             const std::vector<SceneSlotPool::Slot>& slots,
                             const SceneSlotPool& slotPool,
                             size_t sceneIndex) const;
    rs::sim::TrainRuntime& Runtime() { return runtime_; }
    const rs::sim::TrainRuntime& Runtime() const { return runtime_; }
    // 信号の現示。手動の強制R(SetForceRed)の窓口。
    rs::sim::SignalAspectRuntime& SignalAspects() { return signalAspectRuntime_; }
    const rs::sim::SignalAspectRuntime& SignalAspects() const { return signalAspectRuntime_; }
    rs::sim::DiaRuntime& DiaRuntime() { return dia_; }
    const rs::sim::DiaRuntime& DiaRuntime() const { return dia_; }
    const TickStats& LastTickStats() const { return lastTickStats_; }
    const TickStats& LastRenderSubmitStats() const { return lastRenderSubmitStats_; }
    // 直近のTick()で算出した駅舎ごとの接近マスク(駅舎配置のAddress→マスク)。
    // SceneRenderer::Tickが同フレームでSceneSceneryRuntime::Tickへそのまま通す。
    const std::unordered_map<rs::world::Guid, rs::sim::StationApproachMask,
                             rs::world::GuidHash>& LastApproachMasks() const {
        return lastApproachMasks_;
    }

    // 運転台視点のカブピボット(FrontCabin/
    // TailCabin)のワールド剛体変換を解決する。travelReversed(TrainRuntime::
    // GroupTravelReversed)から先頭/後尾どちらの車両・カブを使うか決める(RailSim IIの
    // CTrainGroup::SetCabinView、CTrainGroup.cpp:427-430と同じ考え方)。
    // 対象車両がGPU常駐スロットを持たない(非表示/読込未完了)、または対応する
    // Joint3D targetが解決できない場合はfalseを返す(呼び出し側は前フレームの
    // ビューを保持すること、identityへフォールバックしないこと)。
    bool ResolveCabinPivot(const rs::world::SceneRegistry& registry,
                           const std::vector<SceneSlotPool::Slot>& slots, TrainId trainId,
                           bool travelReversed, bool& outTailCabin,
                           rs::core::Mat4& outPivotWorld) const;

    // HUD用の前駅・次駅・次駅の番線。TickGroupDiaが毎tick解決したPlatformStopInfoを
    // キャッシュしたものを返す(未走行/対象なしは空文字列)。前駅は、目標番線が変わった
    // 瞬間の直前の次駅。TrainRuntime::GroupTargetPlatformを新たに呼び直さない(発車直後の
    // 再捕捉猶予中は古い番線を指したままになりうるラッチのため)。
    struct StopLabels {
        std::string previousStation;
        std::string nextStation;
        std::string nextPlatform;
        std::string lastTargetStation; // 前駅の更新用。対象なしの間も残す最後の次駅
    };
    const StopLabels& GroupStopLabels(TrainId trainId) const;

private:
    using Slot = SceneSlotPool::Slot;
    struct CachedForwardConstraints {
        rs::world::ForwardScanSpan forwardSpan;
        double effectiveMaxVelocityKmh = 0.0;
    };
    struct GroupForwardScan {
        const rs::world::ForwardScanSpan* forwardSpan = nullptr;
        double effectiveMaxVelocityKmh = 0.0;
        bool runFullSearch = false;
        double dynamicSeekAheadM = 0.0;
        bool groupMoving = false;
    };
    struct GroupDoorBase {
        bool base1 = false;
        bool base2 = false;
        DoorSideReference sideReference;
    };

    // SubmitRenderSnapshotがスロットごとの提出へ渡す、フレーム内で共通の入力と計測の集計。
    struct SubmitFrame {
        App* app = nullptr;
        const rs::world::TrackGraph* activeGraph = nullptr;
        const float* eye = nullptr;
        const rs::world::GameClock* clock = nullptr;
        const rs::world::SolarState* sun = nullptr;
        double latitudeDeg = 0.0;
        double nightThresholdSin = 0.0;
        float viewerFovYRadians = 0.0f;
        const WheelSoundFrame* wheelFrame = nullptr;
        std::vector<rs::sim::WheelSoundEvent>* wheelEvents = nullptr;
        size_t batchedCount = 0;
        size_t fallbackCount = 0;
        // 計測用: 実際にGPUメッシュ再アップロードまで進んだスロット数。
        size_t geometryUploadCount = 0;
        // 計測用: フォールバック経路の内訳。
        double fallbackMaterialMs = 0.0;
        double fallbackAnimationMs = 0.0;
        double fallbackPartWorldsMs = 0.0;
    };
    // 車両スロット1台ぶんの描画提出（姿勢の解決・車輪音の判定・バッチ追加）。
    void SubmitCarSlot(SubmitFrame& frame, size_t slotIndex, Slot& slot);
    // バッチに入れられなかったスロットの個別GPU提出。
    void SubmitFallbackSlot(SubmitFrame& frame, size_t slotIndex, Slot& slot);

    GroupForwardScan ResolveGroupForwardSpan(
        const TrainGroupSnapshot& group,
        const std::function<rs::world::TrackGraph*(size_t)>& graphForScene,
        const std::function<const rs::world::TrackGraph*(size_t)>& constGraphForScene,
        const rs::world::WarpLinkTable& warpTable,
        const std::unordered_set<rs::world::Guid, rs::world::GuidHash>& occupiedBlocks,
        const std::unordered_set<rs::world::Guid, rs::world::GuidHash>& previousOccupiedBlocks,
        double nowAbsSeconds);
    // timeScaleはドアを閉じ始める先行時間(DoorClosingTime x TimeScale)に使う。
    rs::sim::DiaTickResult TickGroupDia(const TrainGroupSnapshot& group,
                                        const rs::world::TrackGraph& trainGraph,
                                        double nowAbsSeconds, double timeScale);
    double ComputeGroupEffectiveMaxVelocity(
        const TrainGroupSnapshot& group,
        const std::function<const rs::world::TrackGraph*(size_t)>& constGraphForScene,
        const rs::world::WarpLinkTable& warpTable, const GroupForwardScan& forwardScan,
        const std::vector<rs::sim::OccupancyTrain>& occupancyTrains,
        const std::unordered_set<rs::world::Guid, rs::world::GuidHash>& blocksOccupiedByOthers,
        double dt);
    // TickGroupKinematicsへ渡す入力一式。sim/app境界を跨ぐ関数の引数を1箇所へ集約し、
    // 引数順序の取り違えを防ぐ。
    struct KinematicsInput {
        rs::world::SceneRegistry* registry = nullptr;
        const std::function<rs::world::TrackGraph*(size_t)>* graphForScene = nullptr;
        const rs::world::WarpLinkTable* warpTable = nullptr;
        double dt = 0.0;
        double nowAbsSeconds = 0.0;
        const std::vector<SceneSlotPool::Slot>* slots = nullptr;
        const SignalPhaseOutputs* signalPhase = nullptr;
        // この編成以外が在線している閉塞(ポイント在線ガード用)。
        const std::unordered_set<rs::world::Guid, rs::world::GuidHash>* blocksOccupiedByOthers =
            nullptr;
        double effectiveMaxVelocityKmh = 0.0;
        const rs::world::ForwardScanSpan* forwardSpan = nullptr;
    };
    // TickGroupKinematicsの出力一式(呼び出し前のhead/trainSceneIndexは入力の初期値でもある
    // ため、呼び出し元で保持している変数をそのままここへ入れて渡し、呼び出し後に読み戻す)。
    struct KinematicsResult {
        rs::world::TrackPosition head;
        double dsMeters = 0.0;
        size_t trainSceneIndex = 0;
    };
    void TickGroupKinematics(const TrainGroupSnapshot& group, const KinematicsInput& input,
                              KinematicsResult& result);
    std::vector<rs::legacy_plugin::CarPose> SolveGroupCarPoses(
        const TrainGroupSnapshot& group, const rs::world::SceneRegistry& registry,
        const std::vector<SceneSlotPool::Slot>& slots,
        const std::function<const rs::world::TrackGraph*(size_t)>& constGraphForScene,
        const rs::world::WarpLinkTable& warpTable, size_t trainSceneIndex,
        const rs::world::TrackPosition& head) const;
    GroupDoorBase ResolveGroupDoorBase(
        TrainId trainId, const rs::world::TrackGraph* trainGraph,
        const rs::sim::DiaTickResult& diaResult,
        const std::vector<rs::legacy_plugin::CarPose>& poses) const;
    bool UpdateCarSlotSnapshot(
        SceneSlotPool::Slot& slot, const rs::world::SceneEntity* entity,
        const rs::legacy_plugin::CarPose& pose, size_t index, size_t carCount,
        TrainId trainId, const rs::sim::DiaTickResult& diaResult,
        const GroupDoorBase& doorBase, const rs::world::TrackGraph& activeGraph,
        size_t activeSceneIndex,
        const std::function<const rs::world::TrackGraph*(size_t)>& constGraphForScene,
        const rs::world::WarpLinkTable& warpTable, double dt, double dsMeters,
        bool groupMoving, const std::optional<rs::world::Guid>& cabinLeadCarEntityId);

    // Tickが編成ごとの処理へ渡す、tick内で共通の入力(どれも呼び出し元が所有する)。
    struct TickFrame {
        rs::world::SceneRegistry* registry = nullptr;
        const std::function<rs::world::TrackGraph*(size_t)>* graphForScene = nullptr;
        const std::function<const rs::world::TrackGraph*(size_t)>* constGraphForScene = nullptr;
        const rs::world::WarpLinkTable* warpTable = nullptr;
        size_t activeSceneIndex = 0;
        const rs::world::TrackGraph* activeGraph = nullptr;
        double dt = 0.0;
        double nowAbsSeconds = 0.0;
        std::vector<SceneSlotPool::Slot>* slots = nullptr;
        TrainId cabinViewTrainId;
        double timeScale = 1.0;
        const std::vector<rs::sim::OccupancyTrain>* occupancyTrains = nullptr;
        const SignalPhaseOutputs* signalPhase = nullptr;
    };
    // 編成を順に処理する間に積み上がる、tick全体の集計(接近判定の入力・計測・車両数)。
    struct TickAccumulators {
        std::vector<rs::sim::ApproachTrain> approachTrains;
        std::vector<rs::world::ForwardScanSpan> approachForwardSpans;
        std::chrono::nanoseconds searchTime{};
        std::chrono::nanoseconds kinematicsTime{};
        size_t processedVehicles = 0;
    };
    // 1編成の走行結果(軌道を進めた後の先頭位置とダイヤ状態)。車両スロットの更新へ渡す。
    struct GroupMotion {
        rs::world::TrackPosition head;
        rs::sim::DiaTickResult diaResult;
        double dsMeters = 0.0;
        // warp接近フェード向け。停車中は跨ぎ接近フェードを適用しない。
        bool groupMoving = false;
        size_t trainSceneIndex = 0;
    };
    std::vector<rs::sim::OccupancyTrain> BuildOccupancyTrains(
        const std::vector<TrainGroupSnapshot>& groups,
        const std::function<rs::world::TrackGraph*(size_t)>& graphForScene) const;
    // 編成が載るsceneの軌道が取れたときの処理(先読み・ダイヤ・速度上限・キネマティクス・接近判定の入力)。
    void TickGroupOnGraph(const TrainGroupSnapshot& group, const rs::world::TrackGraph& trainGraph,
                          const TickFrame& frame, TickAccumulators& acc, GroupMotion& motion);
    // 編成の車両姿勢・ドアの基準を解いて、各車両スロットのスナップショットを更新する。
    void UpdateGroupCarSlots(const TrainGroupSnapshot& group,
                             const rs::world::TrackGraph* trainGraph, const TickFrame& frame,
                             const GroupMotion& motion, TickAccumulators& acc);

    mutable std::unordered_set<rs::world::Guid, rs::world::GuidHash> warnedEntityIds_;
    rs::sim::TrainRuntime runtime_;
    rs::sim::DiaRuntime dia_;
    StationRunInfoDispatcher stationRunInfoDispatcher_;
    rs::sim::BlockOccupancyTracker blockOccupancyTracker_;
    // 信号の現示。毎tick在線の再構築後に計算し直す(保存しない)。手動の強制RもここへSetForceRedする。
    rs::sim::SignalAspectRuntime signalAspectRuntime_;
    // ポイントの予約。編成が前方で通るポイントを先に押さえ、取れていないあいだはその区間を守る
    // 信号を停止にする(deniedSignals_)。保存しない。
    rs::sim::RouteReservation routeReservation_;
    std::unordered_map<TrainId, std::unordered_set<rs::world::Guid, rs::world::GuidHash>,
                       rs::world::GuidHash> deniedSignals_;
    SceneTrainBatching batching_;
    TickStats lastTickStats_;
    TickStats lastRenderSubmitStats_;
    std::unordered_map<rs::world::Guid, rs::sim::StationApproachMask,
                       rs::world::GuidHash> lastApproachMasks_;
    std::unordered_map<TrainId, CachedForwardConstraints, rs::world::GuidHash> cachedForwardConstraints_;
    std::unordered_map<TrainId, rs::world::Guid, rs::world::GuidHash> lastKnownTargetPlatform_;
    // GroupStopLabels用。TickGroupDiaがdiaInput.targetPlatformInfoの解決結果を
    // 毎tickここへキャッシュする(クエリ時にgraphアクセスを要らなくするため)。
    std::unordered_map<TrainId, StopLabels, rs::world::GuidHash> lastStopLabels_;
    std::optional<size_t> lastActiveSceneIndex_;
    uint64_t tickCounter_ = 0;
};

} // namespace rs::app

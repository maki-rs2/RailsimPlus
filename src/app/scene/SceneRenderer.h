//
// SceneRenderer.h - SceneRegistryをApp側のシーン常駐スロットへ反映する
// (1配置=1描画)。
//
// slots_はApp::UploadSceneMeshToGpu等(slot番号で指定)へこのクラスが割り当てる形に
// 実装している。スロット数は固定ではなく、空きが無ければ末尾へ新規追加する形で
// 必要な分だけ伸びる(App側はSrvRangeAllocator経由でSRVレンジを遅延確保する)。
// GPU側のSRVプールが尽きた場合のみ配置に失敗する。
//
// category==Trainのスロットは、他カテゴリ(TrackPlugin/Scenery)の剛体1枚
// (ResolveWorldMatrix)とは異なり、軸/台車のchase物理(rs::legacy_plugin::
// SolvePartWorlds)で解決する。これにより曲線上で車体が正しく曲がり、
// trainIdごとに走行(SetGroupTargetSpeed)も可能になる。
//
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../plugin_session/PluginSession.h"
#include "../../plugins/load/TrainAssembly.h"
#include "../../render/mesh/Mesh.h"
#include "../../sim/TrainKinematics.h"
#include "../../sim/TrainRuntime.h"
#include "../../sim/DiaRuntime.h"
#include "../../world/alignment/WarpLinkTable.h"
#include "../../world/scene/SceneRegistry.h"
#include "ScenePicking.h"
#include "SceneSlotSynchronizer.h"
#include "StaticBandedSceneryInstances.h"
#include "StaticMeshCache.h"
#include "SceneTrainController.h"
#include "SignalModelSet.h"
#include "StaticHeadlightRoster.h"
#include "SceneSceneryRuntime.h"
#include "TrainPreviewModel.h"
#include "SceneSceneryPicker.h"
#include "StructLinePoleTracker.h"
#include "SurfaceBoundsTracker.h"
#include "ScenePickItem.h"

namespace rs::app {

class App;
class CameraController;

class SceneRenderer {
public:
    struct PlacementPreviewSpec {
        std::string folderUtf8;
        rs::core::Mat4 world = rs::core::Mat4::Identity();
    };

    // 情景(Struct/Station等)の配置プレビュー。nulloptで消す。
    // 現在のSceneに置かれたSurfaceの範囲(敷設のピンと情景の配置位置の押し戻しに使う)。
    // Surfaceが無い、または範囲が読めないときはnullopt。
    const std::optional<rs::world::SurfaceBounds>& CurrentSurfaceBounds(const rs::world::SceneRegistry& registry) {
        return surfaceBoundsTracker_.Update(registry);
    }
    void SetPlacementPreview(App& app, const std::optional<PlacementPreviewSpec>& preview,
                             std::vector<std::string>& log);

    // 組み立て済みメッシュ1つ(架線の接続プレビュー)を半透明で出す。keyが前回と同じなら
    // buildを呼ばず、GPUのメッシュもそのまま使う(毎フレーム呼んでよい)。メッシュが空なら消す。
    // 情景プレビュー・編成プレビューと同じ描画枠を使うので、同時には出せない。
    void SetMeshPlacementPreview(App& app, const std::string& key,
                                 const std::function<rs::render::Mesh()>& build);
    void ClearMeshPlacementPreview(App& app);

    // 編成配置プレビューの入力。cars/graphは呼び出し中だけ参照する。
    struct TrainPreviewSpec {
        const std::vector<FormationCar>* cars = nullptr;
        const rs::world::TrackGraph* graph = nullptr;
        rs::world::TrackPosition head;
    };
    // 編成の実際の車両モデルを半透明で出す。nulloptまたは描く車が無いときは、編成プレビューを
    // 出している場合に限って消す(情景プレビューの表示は触らない)。モデルのロードとGPUへの
    // アップロードは編成の中身が変わったときだけで、毎フレーム呼んでよい。
    void SetTrainPlacementPreview(App& app, const std::optional<TrainPreviewSpec>& preview,
                                  std::vector<std::string>& log);
    // SceneRegistryとの差分を反映する(追加分をロード/アップロード、削除分を解放)。
    // 空きスロットが無い場合、新規エンティティは無視されlogに警告を出す。
    // category==Trainのエンティティは、ロードに成功すると併せて
    // TrainVehicleTemplateを構築し軸/台車chase解決の対象にする。
    // category==Sceneryのうち静的判定(PluginSession::IsFullyStatic)を通るものは
    // ここではスロットを割り当てず素通りする(EditorViewport.cpp側の
    // GPUインスタンスバッチ再構築が担当)。
    // registryのRevision()が前回Sync時から変わっていなければ何もしない
    // (毎フレーム呼んでも差分が無ければO(1)。大規模レイアウトでの
    // 毎フレームO(entities×slots)走査を避けるため)。
    void Sync(App& app, const rs::world::SceneRegistry& registry, std::vector<std::string>& log);
    bool ConsumeInitialCarBatchTick() {
        return synchronizer_.ConsumeInitialCarBatchTick();
    }

    // レイアウト読込・Scene切替直後のフレーム分割ジョブが進行中か。
    // 呼び出し側はこれがtrueの間、
    // シミュレーション/信号/編成Tick・編集入力・Undo・Scene切替・新規読込要求・
    // 自動復元スナップショットの書き込みを一時停止すること。進捗表示にも使う。
    bool SyncJobActive() const { return synchronizer_.JobActive(); }
    // 現在のフェーズと、そのフェーズ内でのdone/total(進捗オーバーレイのラベル・
    // バー表示に使う、LoadProgressOverlay.cpp)。
    LoadJobPhase SyncJobPhase() const { return synchronizer_.JobPhase(); }
    size_t SyncJobPhaseDone() const { return synchronizer_.JobPhaseDone(); }
    size_t SyncJobPhaseTotal() const { return synchronizer_.JobPhaseTotal(); }

    // 毎フレーム、使用中スロットの各パーツのワールド行列を再計算して反映する
    // (Joint3D階層+mover込み。dtはmover位相更新に使う)。Trainカテゴリはtrain_group_idごとにグループ化し、
    // SolveTrainHeads+SolvePartWorldsで軸/台車chase解決する(走行中グループは
    // TrainRuntime::TickGroupで先頭車headを進め、registry側へ書き戻す)。
    // dtはゲーム内経過秒(呼び出し側=main.cppの固定tickアキュムレータが
    // TimeScale適用済みで渡す)。
    //
    // 単一graphではなく
    // Scene→graphアクセサ(graphForScene)とWarpLinkTableを受け取り、走行系は
    // AdvanceAcrossScenes経由でSceneを跨いで前進できる。activeSceneIndexは
    // 現在GPU描画中のScene(=非Trainエンティティの描画・新規trainの初期
    // sceneIndexに使う)。graphForScene(activeSceneIndex)はeditor.graphを返す
    // ことを呼び出し側の責務とする(scenes[activeIndex].graphは
    // 編集内容を含まない古いコピーのため)。
    // graphForSceneは非constポインタを返す取り決め: TickTrainGroups内のTrainRuntime::TickGroupが、前進するheadが
    // ポイントを背向通過した際にactiveSlotを書き戻すため。
    // clock/sun/latitudeDeg/nightThresholdSin/eye: 配置済みTrain車両のシステム
    // スイッチ(_CAMDIST/時刻系/_VELOCITY等)を更新するために必要。
    // 呼び出し側は毎フレーム同じ値を渡してよい(tick間で変化しない前提、
    // AppRuntime.cppのgameTicksThisFrameループの外で1回計算した値を使い回す)。
    // cabinViewTrainId: 運転台視点がアクティブに
    // 追従中の編成(非アクティブならrs::world::NilGuid())。SceneTrainController::Tickへ
    // そのままそのまま通す(_CABINVIEWシステムスイッチ付与対象の決定に使う)。
    // eye/viewerFovYRadians: 情景スロットの`_CAMDIST`算出に使う視点位置と垂直視野角
    // (SubmitTrainRenderSnapshotと同じ値)。
    void Tick(App& app, rs::world::SceneRegistry& registry,
             const std::function<rs::world::TrackGraph*(size_t)>& graphForScene,
             const rs::world::WarpLinkTable& warpTable, size_t activeSceneIndex, double dt,
             double nowAbsSeconds, uint64_t activeGraphRevision, const rs::world::GameClock& clock,
             const rs::world::SolarState& sun, double latitudeDeg, double nightThresholdSin,
             TrainId cabinViewTrainId, const float eye[3], float viewerFovYRadians);

    // 使用中の非静的Sceneryスロットのうち、時間駆動アニメ(SetAnimation)を持つものへ
    // 現在の再生位相を反映する(描画フレームごとに1回呼ぶこと。固定tickループ内で
    // 呼ぶと同一フレームで重複して書き込むだけで無駄になる)。再生時計の前進自体は
    // Tick()側(session.animationClockSeconds += dt)が担う。
    void UpdateSceneAnimations(App& app);
    void SubmitTrainRenderSnapshot(App& app, const rs::world::TrackGraph& activeGraph,
                                     const float eye[3], const rs::world::GameClock& clock,
                                     const rs::world::SolarState& sun, double latitudeDeg,
                                     double nightThresholdSin, float viewerFovYRadians,
                                     const WheelSoundFrame& wheelFrame,
                                     std::vector<rs::sim::WheelSoundEvent>& wheelEvents);
    void ResetTrainWarpFade();
    // Sceneの並べ替え(oldToNew[旧]=新)に合わせて、走行系が持つScene番号を付け替える。
    void RemapTrainSceneIndices(const std::vector<size_t>& oldToNew);

    // trainIdの編成を目標速度[km/h]で走行させる(0で停止=headを固定。
    // 負値=後退方向への目標速度)。
    // 未配置のgroupIdを指定しても次回Tick時に無視されるだけで安全。
    // 直近のTick()呼び出し1回で、パーツ行列を実際に再計算したスロット数
    // (非Train・非静的Scenery対象のみ)。計測用(FrameProfiler経由でCSV出力)。
    size_t LastTickRecomputedSlots() const { return sceneryRuntime_.LastTickRecomputedSlots(); }
    std::vector<SceneSlotPool::Slot>& Slots() { return synchronizer_.Slots(); }
    const std::vector<SceneSlotPool::Slot>& Slots() const { return synchronizer_.Slots(); }
    double LastTrainTickMs() const { return lastTrainTickMs_; }
    double LastSceneryTickMs() const { return lastSceneryTickMs_; }
    const SceneTrainController::TickStats& LastTrainTickStats() const {
        return trainController_.LastTickStats();
    }
    const SceneTrainController::TickStats& LastTrainRenderSubmitStats() const {
        return trainController_.LastRenderSubmitStats();
    }

    // 信号の現示(実行時状態)。強制Rの切り替えと表示用の参照に使う。
    rs::sim::SignalAspectRuntime& SignalAspects() { return trainController_.SignalAspects(); }
    const rs::sim::SignalAspectRuntime& SignalAspects() const { return trainController_.SignalAspects(); }
    // 信号プラグインのモデル(描画用エンティティ。SceneRegistryには入れない)。
    SignalModelSet& SignalModels() { return signalModels_; }
    const SignalModelSet& SignalModels() const { return signalModels_; }
    // Structの架線接続点(LinePoint)から導出した柱。架線編集の端点と架線メッシュの再構築に使う。
    StructLinePoleTracker& StructLinePoles() { return structLinePoleTracker_; }
    const StructLinePoleTracker& StructLinePoles() const { return structLinePoleTracker_; }

    void SetGroupTargetSpeed(TrainId trainId, double targetSpeedKmh);
    void SetGroupCurrentSpeed(TrainId trainId, double speedKmh);
    double GroupTargetSpeedKmh(TrainId trainId) const;
    double GroupCurrentSpeedKmh(TrainId trainId) const;
    rs::sim::TrainRuntime& TrainRuntime();
    const rs::sim::TrainRuntime& TrainRuntime() const;
    rs::sim::DiaRuntime& DiaRuntime();
    const rs::sim::DiaRuntime& DiaRuntime() const;

    // 自動停止: rs::sim::TrainRuntimeへの委譲。詳細はTrainRuntime.h参照。
    void SetGroupStopDistance(TrainId trainId, double remainingMeters);
    void ClearGroupStopTarget(TrainId trainId);
    bool GroupArrivedAtStop(TrainId trainId) const;
    // 編成の運行状態(rs::sim::DiaRuntime)への委譲。
    rs::sim::DiaState GroupDiaState(TrainId trainId) const;
    bool GroupDepartureAbsSeconds(TrainId trainId, double& outAbsSeconds) const;
    // 運転台視点への委譲。詳細はSceneTrainController.h参照。
    bool ResolveCabinPivot(const rs::world::SceneRegistry& registry, TrainId trainId,
                           bool travelReversed, bool& outTailCabin,
                           rs::core::Mat4& outPivotWorld) const;
    // trainIdの編成の先頭車(走行方向込み)の車体の上面中央のワールド座標。解決できなければfalse。
    // 編成情報カードの追従先に使う(カブのJointを持たない車両でも解決できる)。
    bool ResolveLeadCarAnchor(const rs::world::SceneRegistry& registry, TrainId trainId,
                              bool travelReversed, rs::core::Vec3& outAnchor) const;
    // カメラが列車を追跡中なら、先頭車の今の位置へ基準点を合わせる。先頭車が見つからなければ追跡をやめる。
    // そのあと、基準点をSurfaceの範囲の中へ押し戻す(Surfaceが無ければ押し戻さない)。
    void UpdateCameraFollow(const rs::world::SceneRegistry& registry, CameraController& camera);
    // HUD用の前駅・次駅・次駅の番線(SceneTrainController::GroupStopLabels)。
    const SceneTrainController::StopLabels& GroupStopLabels(TrainId trainId) const;
    int PlatformRoutineCursorIndex(
        const rs::world::Guid& platformId,
        const std::vector<rs::world::PlatformRoutineElement>& routine,
        int layerKey = rs::world::kCommonRoutineLayerKey,
        TrainId trainId = rs::world::NilGuid()) const;
    int AddTrainTag(const std::string& label);
    void RestoreTrainTag(rs::world::TrainTagDef tag);
    // 別のレイアウトへ移るとき、走行状態を全部捨てる(SceneTrainController::ResetForNewLayout)。
    void ResetForNewLayout();
    void RemoveTrainTag(int tagId);
    const std::vector<rs::world::TrainTagDef>& TrainTagDefs() const;
    const std::vector<int>& TrainTagsOf(TrainId trainId) const;
    const std::unordered_map<TrainId, rs::world::TrainRunInfo, rs::world::GuidHash>& TrainRunInfoAssignments() const;
    void SetTrainRunInfo(TrainId trainId, rs::world::TrainRunInfo info);

    // trainIdの編成を丸ごと削除する: 所属する全SceneEntityをregistryから
    // 取り除き、走行状態(runningGroups_)も消す。GPUスロットの解放自体は次回
    // Sync()が registry との差分から自動検知して行う(即時解放はしない)。
    void RemoveGroup(TrainId trainId, rs::world::SceneRegistry& registry);
    bool WouldTrainOverlap(const rs::world::SceneRegistry& registry,
                             const rs::world::TrackGraph& graph,
                             const rs::world::TrackPosition& candidateHead,
                             double candidateLengthM, size_t sceneIndex) const;

    // folderUtf8+switchOptionsの組が静的配置(GPUインスタンス化)対象か判定する。
    // 同じ組み合わせは初回のみLoadFolder+ApplySwitchOptionsAndBakeして判定・結果を
    // キャッシュする(2回目以降はディスク再読込しない)。eligible==trueのときのみ
    // *outMeshへ(スイッチ確定値を焼き込み済みの)組立メッシュを返す
    // (EditorViewport.cppのInstanceBatchManager::EnsureBatchへそのまま渡す用)。
    // Sync()自身の判定にも使う(同じキャッシュを共有し二重ロードを避ける)。
    // 同一folderでもswitchOptionsが異なれば別キャッシュ・別バッチにする必要がある
    // (PluginSession::ApplySwitchOptionsAndBakeのコメント参照)。
    bool TryGetStaticInstanceMesh(const std::string& folderUtf8,
                                  const std::vector<int32_t>& switchOptions,
                                  std::vector<std::string>& log, const rs::render::Mesh** outMesh,
                                  bool signalModel = false, const std::string& definitionFile = std::string());

    // 静的バッチで描いている個体のうち、灯具を持つものの一覧(ViewportSceneryInstances が積み直しのたびに更新)。
    StaticHeadlightRoster& StaticHeadlights() { return staticHeadlights_; }
    const StaticHeadlightRoster& StaticHeadlights() const { return staticHeadlights_; }
    // TryGetStaticInstanceMeshの拡張版(StaticMeshCache::TryGetStaticInstanceVariants参照)。
    bool TryGetStaticInstanceVariants(const std::string& folderUtf8,
                                      const std::vector<int32_t>& switchOptions,
                                      std::vector<std::string>& log,
                                      const StaticMeshVariantSet** outVariants, bool signalModel = false, const std::string& definitionFile = std::string());

    // TryGetStaticInstanceMesh/InstanceBatchManagerのキャッシュ・バッチキーとして使う
    // folder+スイッチ確定値の複合キー文字列を組み立てる。
    static std::string MakeStaticInstanceKey(const std::string& folderUtf8,
                                             const std::vector<int32_t>& switchOptions, bool signalModel = false, const std::string& definitionFile = std::string());

    // _CAMDISTまたは共有状態(_NIGHT/_SEASON/_SHADOW/_ENVMAP)で切り替わる静的配置(バリアント複数)の管理
    // (リビルド時の登録先)。
    StaticBandedSceneryInstances& BandedStaticInstances() { return bandedStaticInstances_; }

    // 視点と共有状態の値を保存し、バンドか共有状態でバリアントが変わった静的配置のバッチを
    // 積み直す(毎フレーム)。更新間隔はRenderTuningのcamDistBandUpdateHz。共有状態が変わった
    // フレームは間隔を待たない。
    void UpdateBandedStaticInstances(App& app, const float eye[3], float viewerFovYRadians,
                                     const rs::world::SceneSharedSwitchValues& shared);
    // 保存済みの視点と共有状態で積み直す(リビルド直後の呼び出し用。AddMember直後は更新間隔に
    // 関わらず必ず更新が走る)。
    void ApplyBandedStaticInstances(App& app);

    // TrackPluginの離散反復配置(Pier等、EditorViewport.cpp)向けキャッシュ。
    // folderUtf8単位でLoadFolder結果(plugin.assemblyMesh)を1回だけ読み込み、以後の
    // 再配置/リビルドではディスク再読込しない(敷設エディタで編集のたびに
    // Pier等をLoadFolderし直すと操作が固まるため)。
    // TryGetStaticInstanceMeshと異なりIsFullyStatic()判定は行わず、switchOptionsも
    // 扱わない(TrackPluginPlacementItemはswitchOptionsを持たない。TrackPlugin
    // 配置ループの判定はok&&hasAssemblyとする)。
    bool TryGetTrackPluginAssemblyMesh(const std::string& folderUtf8,
                                      std::vector<std::string>& log,
                                      const rs::render::Mesh** outMesh);

    // Pier(橋脚)専用: folderUtf8 + 実際の設置点高さ(heightM)でアセンブリメッシュを
    // キャッシュする(StaticMeshCache::TryGetPierAssemblyMeshへ委譲)。
    bool TryGetPierAssemblyMesh(const std::string& folderUtf8, float heightM,
                                std::vector<std::string>& log, const rs::render::Mesh** outMesh);

    // folderUtf8のプラグインが宣言するスイッチ定義(DefineSwitch、宣言順)を返す
    // (StaticMeshCache::TryGetSwitchDefsへ委譲、配置スイッチ表示パネル向け)。
    const std::vector<rs::legacy_pi::SwitchDef>* TryGetSwitchDefs(const std::string& folderUtf8,
                                                                   std::vector<std::string>& log);
    const rs::legacy_pi::sidecar::StationSidecar& TryGetStationSidecar(const std::string& folderUtf8);

    // 静的メッシュキャッシュの上限超過分を破棄する(StaticMeshCache::PruneStaticMeshCacheへ
    // 委譲)。TryGetStaticInstanceMeshが返した`const Mesh*`を1つも保持していない地点
    // からのみ呼ぶこと(理由は委譲先のコメント参照)。
    // liveKeysに含まれるキーは上限超過でも破棄しない(委譲先のコメント参照)。
    void PruneStaticMeshCache(const std::unordered_set<std::string>& liveKeys);

    // trackPluginAssemblyCache_の上限超過分を破棄する
    // (StaticMeshCache::PruneTrackPluginAssemblyCacheへ委譲)。呼び出しの約束事は
    // PruneStaticMeshCacheと同じ(TryGetTrackPluginAssemblyMeshが返したポインタを
    // 1つも保持していない地点からのみ呼ぶこと)。
    void PruneTrackPluginAssemblyCache(const std::unordered_set<std::string>& liveKeys);

    // pierAssemblyCache_の上限超過分を破棄する
    // (StaticMeshCache::PrunePierAssemblyCacheへ委譲)。呼び出しの約束事は
    // PruneTrackPluginAssemblyCacheと同じ。
    void PrunePierAssemblyCache(const std::unordered_set<std::string>& liveKeys);

    // [switchprof] 計測専用: 静的判定キャッシュの現在エントリ数。
    size_t StaticClassCacheSize() const { return staticMeshCache_.StaticClassCacheSize(); }
    bool TakePluginCacheChange();

    // Scenery選択ピッキングの共通基盤(駅舎/施設等を含む)。registry内のcategory==Scenery全件から
    // ScreenPickCandidate(ワールド行列+モデル実寸AABB)を組み立てる。AABBの取得元は
    // 2経路: 静的判定を通るもの(GPUインスタンスバッチ側)はTryGetStaticInstanceMesh
    // のキャッシュ、アニメ等で非静的なもの(slots_側)はセッションの組立メッシュの
    // コピーへ静的部分ワールドを焼き込んでAABBを求めSlotへキャッシュする。メッシュ
    // 未取得の項目はhasAabb=falseのまま返す(呼び出し側のPickByScreenRectが原点+
    // 固定半径へフォールバックする)。
    // registry.Revision()が前回呼び出し時から変わっていなければ列挙をスキップし
    // 前回の結果を使い回す(Scenery entityの設置座標はUpdatePlacementで動かない
    // 不変を前提にした最適化、SceneRenderer::Syncと同じ考え方。ホバー判定は
    // 毎フレーム呼ばれるため大規模レイアウトでのフルスキャン連発を避ける)。
    // スコープ注記: Train(車両)は台車chase物理でワールド行列がTick()内の一時計算
    // にしかならず、本関数と同じ単純な列挙では対応できないため対象外
    // (Train用は下のCollectTrainPickCandidatesを使う)。
    using ScenePickItem = rs::app::ScenePickItem;
    void CollectSceneryPickCandidates(const rs::world::SceneRegistry& registry,
                                      const rs::world::TrackGraph& graph,
                                      std::vector<std::string>& log,
                                      std::vector<ScenePickItem>& out);

    // 1件だけ(entityId指定)欲しい場合の軽量版(ホバー/選択のハイライト描画用、
    // 全Scenery走査を避ける)。categoryがSceneryでない/entityId未存在ならfalse。
    bool TryGetSceneryPickCandidate(const rs::world::SceneRegistry& registry,
                                    const rs::world::TrackGraph& graph,
                                    const rs::world::Guid& entityId, std::vector<std::string>& log,
                                    ScenePickItem& out);

    // Scenery選択ハイライト用(非静的=slots_側)。
    // entityIdがslots_に割当済みならそのスロット番号を返す(未割当ならfalse。
    // 静的インスタンス化対象のentityIdは常にfalseを返す=そもそもslots_を
    // 持たない、SceneRenderer::Sync参照)。
    bool FindHighlightSlotForEntity(const rs::world::Guid& entityId, UINT& outSlot) const {
        const int slot = FindSlotByEntityId(entityId);
        if (slot < 0) return false;
        outSlot = static_cast<UINT>(slot);
        return true;
    }

    // entityIdの個体が載っているスロット(無ければnullptr)。スロットはSync()で中身が入れ替わり
    // (GrowSlotでのムーブ・park・再読込)、PluginSession*を含めて保持できないので、使う側が毎フレーム引き直す。
    SceneSlotPool::Slot* FindSlotForEntity(const rs::world::Guid& entityId);
    PluginSession* FindSessionByEntityId(const rs::world::Guid& entityId);
    // 配置に失敗した(読込失敗・組立メッシュ無し・GPU容量不足)個体か。
    bool IsEntityPlacementFailed(const rs::world::Guid& entityId) const {
        return synchronizer_.SlotPool().IsEntityFailed(entityId);
    }
    // 一時的な配置物のスロットで日付・時刻・_SEASONを書くか(SceneSceneryRuntime参照。既定は書く)。
    void SetWriteCalendarForTemporary(bool write) {
        sceneryRuntime_.SetWriteCalendarForTemporary(write);
    }
    // 配置物の表示メッシュを外から渡す(押し出し表示など。ExternalSceneMeshes.h)。配置物を消すときはEraseする。
    void SetExternalMesh(const rs::world::Guid& entityId, std::shared_ptr<const rs::render::Mesh> mesh) {
        externalMeshes_.Set(entityId, std::move(mesh));
    }
    void EraseExternalMesh(const rs::world::Guid& entityId) { externalMeshes_.Erase(entityId); }
    // sourceの生パース結果(未登録ならここで読む)。スロットの読込と同じキャッシュなので、先にここで
    // 読んでおけば、同じsourceのスロットの読込はキャッシュヒットになる。
    std::shared_ptr<const rs::legacy_plugin::LoadedPlugin> EnsureParsedPlugin(const PluginSource& source) {
        return staticMeshCache_.ParsedPlugins().EnsureParsed(source);
    }

    // Train(編成車両)のホバーハイライト専用ピッキング基盤。
    // RailSim IIでは配置・撤去のハイライトと配置プラグインのスイッチ編集が別系統で、
    // 車両選択はスイッチ編集側で足りるためここでは実装しない。
    // 候補の箱は、描画と同じパーツ行列から車体の行列を基準に作る(TrainPickBox、
    // SubmitRenderSnapshotが毎フレーム更新)。曲線でも描画される車体に沿う。
    // Trainの実位置は毎フレーム変わるため
    // Scenery側のようなrevisionベースのキャッシュは行わず、呼び出しの都度slots_を
    // 走査する(編成数は少数を想定、CollectSceneryPickCandidatesほどの規模を想定
    // しないためコストは問題にならない)。
    void CollectTrainPickCandidates(const rs::world::SceneRegistry& registry,
                                      std::vector<ScenePickItem>& out) const;
    bool TryGetTrainPickCandidate(const rs::world::SceneRegistry& registry,
                                    const rs::world::Guid& entityId, ScenePickItem& out) const;

private:

    using Slot = SceneSlotPool::Slot;
    // スロットの保持・割当・保管(park/revive/evict)はSceneSlotPoolが受け持つ。
    // GPUリソースの実際の解放(App::ClearSceneSlot)・可視化切替(App::SetSceneSlotVisible)は
    // Sync()側がApp経由で行う(SceneSlotPool自体はGPU/App非依存)。
    SceneSlotSynchronizer synchronizer_;
    ExternalSceneMeshes externalMeshes_;
    // 走行中の編成(trainId)ごとのキネマティクス状態(head/speed)を保持・
    // 駆動するGPU非依存の実行時状態(rs::sim層、レイヤー分離のため独立させている)。
    SceneTrainController trainController_;
    SceneSceneryRuntime sceneryRuntime_;
    SceneSceneryPicker sceneryPicker_;
    SurfaceBoundsTracker surfaceBoundsTracker_;
    StructLinePoleTracker structLinePoleTracker_;
    double lastTrainTickMs_ = 0.0;
    double lastSceneryTickMs_ = 0.0;

    // slots_に依存しない静的メッシュ分類キャッシュ(StaticMeshCache.h参照)。
    StaticMeshCache staticMeshCache_;
    uint64_t viewportPluginGeneration_ = 0;
    SignalModelSet signalModels_;
    StaticHeadlightRoster staticHeadlights_;
    StaticBandedSceneryInstances bandedStaticInstances_;
    // UpdateBandedStaticInstancesで最後に受け取った共有状態の値(リビルド直後の積み直しが使う)。
    rs::world::SceneSharedSwitchValues lastSharedSwitches_;
    PluginSession placementPreviewSession_;
    // GPUへアップロード済みのプレビューメッシュの中身を表すキー("S|"+folder=情景、
    // "T|"+編成の中身=編成)。種類を含めるので、情景と編成を行き来しても取り違えない。
    std::string placementPreviewKey_;
    TrainPreviewModel trainPreviewModel_;
    // 編成プレビューを出している(SetActive(true)した)か。情景プレビューの表示を巻き込んで
    // 消さないための印。
    bool trainPreviewShown_ = false;
    // SetMeshPlacementPreviewが組み立て済みメッシュを出している間true。
    bool meshPreviewShown_ = false;

    // 前回Sync()実行時のregistry.Revision()。一致していれば構成が変化していないので
    // Sync()を丸ごとスキップできる(revision_は0開始のため、UINT64_MAXを初期値にして
    // 初回は必ず不一致にする)。

    // LastTickRecomputedSlots()用(Tick()冒頭で0にリセットし、再計算した分だけ加算)。

    int FindSlotByEntityId(const rs::world::Guid& id) const {
        return synchronizer_.FindSlotByEntityId(id);
    }

};

} // namespace rs::app

//
// SceneSlotPool.h - SceneRenderer::Sync()が管理するシーン常駐スロット(GPU描画枠)の
// 保持・割当・保管(park)を担うプールクラス。
//
// Slot構造体・スロット割当・失敗記録を受け持つ。ここではGPU/App層
// (FrameRenderer::ClearSceneSlot等)には触れず、スロットの「どれが空いているか/
// どれが保管中か/保管中スロットのうちどれが同じ内容(contentKey)を再利用できるか」
// というブックキーピングのみを担う(レイヤー分離)。GPUリソースの実際の解放
// (ClearSceneSlot)・可視化切替(SetSceneSlotVisible)はSceneRenderer::Sync側が
// App経由で行う。
//
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../plugin_session/PluginSession.h"
#include "StaticInstanceKey.h"
#include "TrainPickBox.h"
#include "../../plugins/load/TrainAssembly.h"
#include "../../sim/TrainKinematics.h"
#include "../../core/Math.h"
#include "../../render/pipeline/MeshPipeline.h"
#include "../../world/scene/SceneEntity.h"

namespace rs::app {

class SceneSlotPool {
public:
    // Train車両の描画スナップショット(fixed tickで確定した最終車両状態、
    // および台車テンプレート/アニメーション状態)。category==Trainのみ有効。
    struct CarRenderSnapshot {
        rs::legacy_plugin::TrainVehicleTemplate trainTemplate;
        rs::legacy_plugin::AxleSpinState axleSpin;
        rs::sim::TiltDriverState tiltState;
        // 車両headの代表ワールド行列(TickTrainGroupsが台車chase解決後の
        // heads[k]から毎tick更新する)。カメラ距離の基準点と、車体が無い縮退プラグインの
        // 箱の基準に使う。
        rs::core::Mat4 lastCarWorld = rs::core::Mat4::Identity();
        // 赤枠とピッキングに使う箱。SubmitRenderSnapshotが、描画と同じパーツ行列から
        // 毎フレーム更新する(UpdateCarPickBox)。箱は車体の行列(carPickFrame)のローカル座標。
        // carPickValidがfalseの間(未更新や対象パーツ無し)はピッキング候補から外す。
        rs::core::Mat4 carPickFrame = rs::core::Mat4::Identity();
        float carPickAabbMin[3] = {0.0f, 0.0f, 0.0f};
        float carPickAabbMax[3] = {0.0f, 0.0f, 0.0f};
        bool carPickValid = false;
        // パーツごとのローカルAABBの覚え書き。合成メッシュが変わる(スロットの割り当て、
        // ChangeModelでの差し替え)たびにcarPartBoundsDirtyを立てて作り直す。
        std::vector<PartLocalBounds> carPartBounds;
        bool carPartBoundsDirty = true;
        // TickTrainGroupsが毎tick更新する、このスロットが今のフレームで描画対象
        // (=GPUスロットがvisible)かどうか。非表示中は古い箱のままなので
        // ピッキング候補から外す(CollectTrainPickCandidates/TryGetTrainPickCandidate)。
        bool carVisible = true;
        float carFadeAlpha = 1.0f;
        double warpFadeDistanceM = 0.0;
        bool warpFadeInActive = false;
        // fixed tick で確定した描画用の最終車両状態。描画依存の姿勢・ランタイム解決は
        // render frame ごとに一度だけ行う。
        rs::world::TrackPosition carSnapshotHead;
        bool carSnapshotReversed = false;
        double carSnapshotPathOffsetM = 0.0;
        bool carSnapshotConnectFront = false;
        bool carSnapshotConnectRear = false;
        std::map<std::string, int32_t> carSnapshotRoutineSwitchOverrides;
        rs::world::TrainId carSnapshotTrainId = rs::world::NilGuid();
        double carSnapshotDeltaSeconds = 0.0;
        double carSnapshotDistanceMeters = 0.0;
        std::vector<rs::render::PartXform> carPartWorlds;
        // 車輪音の継目判定（SceneTrainWheelSound.cpp）。carAxleContactsは描画フレームごとに
        // SolvePartWorldsが書く。直前の描画で覚えた編成の通算走行距離は、スロットの持ち主
        // （編成とスロットの世代）が変わったら使わない。
        std::vector<rs::legacy_plugin::AxleContact> carAxleContacts;
        double lastWheelOdometerM = 0.0;
        bool wheelOdometerValid = false;
        rs::world::TrainId wheelOdometerTrainId = rs::world::NilGuid();
        uint64_t wheelOdometerGeneration = 0;
        // SolvePartWorldsの生の戻り値(session.plugin.def.parts添字、mover適用前)。
        // カブ視点のFrontCabin/TailCabin
        // ピボット解決(SceneTrainController::ResolveCabinPivot)が、RailSim IIの
        // CObjectJoint3D::GetPos等と同じくBody自身の物理解決済み姿勢(mover非適用)を
        // 参照するために保持する。SubmitRenderSnapshot(SceneTrainRenderSubmit.cpp)が
        // 毎フレーム上書きする。
        std::vector<rs::core::Mat4> carDefPartWorlds;
        bool carSnapshotValid = false;
        // DynamicModel により geometry が変化した。個別スロット提出時に GPU mesh を更新する。
        bool carGeometryDirty = false;
        // 現在のgeometryに対しUploadSceneMeshToGpuが失敗した(例: assemblyMeshが空)。
        // 一度失敗した内容は毎フレーム再試行しても結果が変わらないため、次にDynamicModel
        // でgeometryが変わる(carGeometryDirty再設定)までは個別GPUアップロードを
        // スキップする(同一内容の毎フレーム同期アップロード再試行を防ぐ)。
        bool carGpuMeshUploadFailed = false;
    };

    // category==Sceneryのみ有効。Tick()が最新のswitch/mover解決済みパーツワールドを
    // session.plugin.assemblyMeshの「コピー」へBakeStaticPartWorldsで焼き込み、AABBを
    // ここへ更新する。Tick()の配置・グラフ・可動部の不変スキップが成立する間は
    // このキャッシュも使い回す。焼き込み前の生
    // assemblyMeshはパーツがローカル原点付近に重なった状態のため、これに
    // 直接computeAabb()するとパーツ多数のオブジェクトで実寸と大きくズレた
    // AABBになる。Tick()側は
    // assemblyMesh自体を実行時にgPartWorld適用して描画するため、コピー側
    // にのみ焼き込み、slot.session.plugin.assemblyMesh本体は変更しない。
    struct SceneryPickBounds {
        bool sceneryPickAabbCached = false;
        bool sceneryPickHasAabb = false;
        float sceneryPickAabbMin[3] = {0.0f, 0.0f, 0.0f};
        float sceneryPickAabbMax[3] = {0.0f, 0.0f, 0.0f};
    };

    // 保管(park)/復活(revive)機構。
    // Scene切替のたびにSceneRegistryが「全削除→全追加」される都合上、中身が
    // 変わっていない配置でもentityIdが変わり、保管しないとGPUジオメトリ/テクスチャ
    // プールごと破棄(ClearSceneSlot、gpu.WaitForGpu()込み)→LoadFolderから
    // やり直しになる。folderUtf8+switchOptions+categoryが一致するentityが
    // 再度現れたときに、session/GPUリソースを保持したまま復活できるよう、
    // 保管時点のentityの内容を記録しておく。
    struct SlotResidencyState {
        std::string          folderUtf8;
        // 読んだ定義ファイルのファイル名(SceneEntity::definitionFile。空=既定の選び方)。
        // folderUtf8と組でプラグインの出どころ(PluginSource)を表す。
        std::string          definitionFile;
        std::vector<int32_t> switchOptions;
        rs::world::SceneEntityCategory category = rs::world::SceneEntityCategory::Scenery;
        // MakeSlotContentKey(配置物)で作る複合キー(フォルダ・定義ファイル名・スイッチ確定値)。retrieval用に事前計算して
        // 持たせておく(park登録のたびに文字列組み立てし直さなくて済むように)。
        std::string contentKey;
        // スロットが今どの配置物の内容を持っているかを記録する(復活・再利用の照合に使う)。
        void AssignContent(const rs::world::SceneEntity& entity, const std::string& key) {
            folderUtf8 = entity.folderUtf8;
            definitionFile = entity.definitionFile;
            switchOptions = entity.switchOptions;
            category = entity.category;
            contentKey = key;
        }
        // このスロットが現在parked(GPU/sessionは保持したまま非表示・未使用)かどうか。
        bool parked = false;
        // parkされたrevision番号(SceneRegistry::Revision())。空きスロット・retrieval
        // 対象が無い場合のLRU eviction対象選定(最も古いparkedAtRevisionを持つものを
        // 追い出す)に使う。
        uint64_t parkedAtRevision = 0;
        // このスロットが使用開始/park復活(revive)されるたびにインクリメントする。
        // 音源要求キー等、スロットindexに依存する識別子の世代識別に使う。
        uint64_t generation = 0;
    };

    struct Slot {
        // CarRenderSnapshotのstd::mapのせいで暗黙のムーブがnoexceptにならず、vector<Slot>の
        // 拡張(GrowSlot)が全スロットのPluginSession込みの深いコピーになってしまう。
        // そのため、PluginSessionと同じくムーブを明示してnoexceptにする。
        Slot() = default;
        Slot(const Slot&) = default;
        Slot& operator=(const Slot&) = default;
        Slot(Slot&&) noexcept = default;
        Slot& operator=(Slot&&) noexcept = default;

        bool used = false;
        rs::world::Guid entityId = rs::world::NilGuid();
        PluginSession session;
        // category==Trainのみ有効。
        bool isTrainCar = false;
        // Train のスロット固有メッシュが GPU に存在するか。初回バッチ候補は
        // false のまま保持し、同フレームのバッチ化に失敗した場合だけ遅延作成する。
        bool hasGpuMesh = false;
        CarRenderSnapshot carSnapshot;
        SceneryPickBounds sceneryPick;
        SlotResidencyState residency;
        // Tick()側の再計算スキップ判定用(category==Sceneryのみ有効)。true の間は
        // Tick()が entityWorld 再計算・mover状態からのパーツ行列合成・
        // App::SetScenePartWorlds を必ず行う。false は「前回計算した
        // cachedEntityWorld と gPartWorld 相当の中身が現在も有効」を表す。
        // 初期値はtrue(初回は必ず計算する)。スロットを新規に使い始める箇所・
        // parkから復活(revive)させる箇所は明示的にtrueへ戻すこと(既定の
        // Slot{}構築で自然にtrueになる経路以外は書き忘れに注意)。
        bool partWorldsDirty = true;
        // 直前にTick()が計算したentityWorld(比較用キャッシュ)。
        rs::core::Mat4 cachedEntityWorld = rs::core::Mat4::Identity();
        uint64_t cachedPlacementRevision = UINT64_MAX;
        uint64_t cachedGraphRevision = UINT64_MAX;
        // 個別パイプラインへ材質を反映した世代。バッチの世代とは分けて、描画先の切替時にも
        // スイッチの変更を取りこぼさない。UINT64_MAX=未適用。
        uint64_t appliedMaterialSwitchGeneration = UINT64_MAX;
        // category==SceneryのChangeModel実行時反映用(Car側carSnapshot.carGeometryDirtyと
        // 同じ役割)。UpdateDynamicModelsがtrueを返したフレームで立て、GPU再アップロード
        // (UploadSceneMeshToGpu)が成功したらfalseへ戻す。
        bool sceneryGeometryDirty = false;
        // sceneryGeometryDirty立ち上がり後、GPUメッシュ再アップロードに失敗し続ける状態になった
        // (assemblyMeshが空等)ことを示す。trueの間はUploadSceneMeshToGpuを再試行しない
        // (Car側carSnapshot.carGpuMeshUploadFailedと同じ役割)。
        bool sceneryGpuMeshUploadFailed = false;
    };

    std::vector<Slot>& Slots() { return slots_; }
    const std::vector<Slot>& Slots() const { return slots_; }
    size_t Size() const { return slots_.size(); }
    // GPU資源の解放後に呼ぶ。音源などの要求キーが再一致しないよう、スロット世代は引き継ぐ。
    void InvalidatePlugins();

    // used==trueかつentityIdが一致するスロットのindexを返す(無ければ-1)。
    // parked中のスロットはused==falseのため対象外。
    int FindSlotByEntityId(const rs::world::Guid& id) const;

    // 使用可能な空きスロット(!used && !parked)を探す(無ければ-1)。
    int FindFreeSlot() const;

    // 末尾へ新規スロットを追加し、そのindexを返す(FindFreeSlot()もparked retrieval
    // も失敗した場合の最終手段)。
    size_t GrowSlot();

    // index番目のスロットをparkedプールへ登録する(呼び出し側が事前にslots_[index]の
    // parked=true/used=false/folderUtf8/switchOptions/category/contentKey/
    // parkedAtRevisionを設定済みであること)。内部の(category,contentKey)複合キー
    // インデックスへ追加するだけの薄いラッパー。
    void RegisterParked(size_t index);

    // (category, contentKey)が一致するparkedスロットを1件取り出す(あれば該当
    // スロットのparked=falseへ戻しindexを返す。無ければ-1。呼び出し側は残りの
    // フィールド(used/entityId等)を復活処理として設定すること)。
    // 1キーに複数スロットが対応しうるため(同一プラグイン+スイッチ組の配置が多数)
    // multi-index(vector)のフリーリストから1件popする(1キー1スロットの
    // mapだと数百件規模の配置で効果がほぼ出ないため、multi-indexにしている)。
    int TakeParkedSlot(rs::world::SceneEntityCategory category, const std::string& contentKey);

    // (category, folderUtf8, definitionFile)が一致するparkedスロットを1件取り出す(あれば該当
    // スロットのparked=falseへ戻しindexを返す。無ければ-1)。TakeParkedSlot()の
    // contentKey完全一致(folder+switchOptions)より緩いフォールバック: スイッチ値
    // だけが変わった配置でも、同じフォルダをLoadFolder済みのparkedスロットを
    // 見つけてセッション再利用(呼び出し側でApplySwitchOptionsAndBakeし直す)できる
    // ようにする。全カテゴリ(Car含む)が対象。
    int TakeParkedSlotByFolder(rs::world::SceneEntityCategory category, const std::string& folderUtf8,
                               const std::string& definitionFile = std::string());

    // 最も古い(parkedAtRevisionが最小の)parkedスロットのindexを返す(parkedスロットが
    // 一件も無ければ-1)。呼び出し側はGPU解放(App::ClearSceneSlot)を行った上で
    // ReleaseParked()し、空きスロットとして再利用すること。
    int FindOldestParkedSlot() const;

    // index番目のスロットのparked登録を(category,contentKey)インデックスから外す
    // (eviction、または完全解放の直前に呼ぶ。呼び出し側がslots_[index]の内容を
    // クリアする前後どちらで呼んでも良いが、contentKey/categoryを読める間に呼ぶこと)。
    void ReleaseParked(size_t index);

    // GPU常駐スロット(SRVプール)枯渇等で一度配置に失敗したentity.guidの集合。
    // 記録せず毎フレーム再試行すると、容量超過分のentityが存在し続ける限り
    // 全滅するまで無限にLoadFolder/GPUアップロードを繰り返す「リトライ嵐」になる
    // registryから削除されれば併せて忘れ、再追加時は再試行する。
    // 失敗した時点の内容キー(MakeSlotContentKey)を一緒に持ち、フォルダやスイッチ値が変わった個体は
    // PruneFailedEntitiesで失敗扱いを解いて再試行できるようにする。
    bool IsEntityFailed(const rs::world::Guid& id) const;
    void MarkEntityFailed(const rs::world::Guid& id, std::string contentKey);
    void ForgetEntityFailed(const rs::world::Guid& id);

    // GPU常駐スロットの有無に関わらず、entityId単位で最後に成功した
    // TrainVehicleTemplateを保持するキャッシュ。GPU常駐
    // スロットの容量確保に失敗しslotが破棄された後も、編成の性能値計算
    // (trainLengthM/maxVelocityKmh等)には最後に読み込めたテンプレートを使い続ける
    // (GPUスロットの常駐・非常駐が編成長・性能制約に影響してはならないため)。
    void RecordCarTemplate(const rs::world::Guid& entityId,
                            const rs::legacy_plugin::TrainVehicleTemplate& carTemplate);
    const rs::legacy_plugin::TrainVehicleTemplate* FindCachedCarTemplate(
        const rs::world::Guid& entityId) const;
    // stillFailsPredicate(id, 失敗時の内容キー)がfalseを返すentityIdを失敗記録から取り除く
    // (registryから消えた、または内容が変わったentityの失敗記録を忘れる)。
    template <typename Predicate>
    void PruneFailedEntities(Predicate stillFailsPredicate) {
        for (auto it = failedEntities_.begin(); it != failedEntities_.end();) {
            it = stillFailsPredicate(it->first, it->second) ? std::next(it) : failedEntities_.erase(it);
        }
    }

private:
    static std::string MakeParkedIndexKey(rs::world::SceneEntityCategory category,
                                          const std::string& contentKey);
    static std::string MakeParkedFolderKey(rs::world::SceneEntityCategory category,
                                           const std::string& folderUtf8,
                                           const std::string& definitionFile);

    // indices中からindexを1件探して取り除く(見つからなければ何もしない)。
    // parkedByKey_/parkedByFolder_どちらも「複数キーが同じスロットを指す」
    // 二重インデックスのため、片方から取り出したら必ずもう片方からも
    // 除去する必要があり、その共通処理。
    static void EraseIndex(std::vector<size_t>& indices, size_t index);

    std::vector<Slot> slots_; // 固定数ではなく必要な分だけ末尾へ伸びる
    // entityId -> 失敗した時点の内容キー。
    std::unordered_map<rs::world::Guid, std::string, rs::world::GuidHash> failedEntities_;
    // entityId -> 最後に成功したTrainVehicleTemplate(GPU常駐/非常駐に関わらず保持)。
    std::unordered_map<rs::world::Guid, rs::legacy_plugin::TrainVehicleTemplate, rs::world::GuidHash> lastKnownCarTemplates_;
    // (category,contentKey)複合キー -> parked中のスロットindex一覧(フリーリスト)。
    std::unordered_map<std::string, std::vector<size_t>> parkedByKey_;
    // (category,フォルダ+定義ファイル名)複合キー -> parked中のスロットindex一覧(フリーリスト)。
    // parkedByKey_と同じスロット集合を指す二重インデックス(TakeParkedSlotByFolder用)。
    std::unordered_map<std::string, std::vector<size_t>> parkedByFolder_;
};

} // namespace rs::app

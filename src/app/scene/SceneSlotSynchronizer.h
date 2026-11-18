#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "ExternalSceneMeshes.h"
#include "SceneSlotPool.h"
#include "StaticMeshCache.h"
#include "../plugin_session/PreloadTextureSet.h"
#include "../../world/scene/SceneRegistry.h"

namespace rs::app {

class App;
class StaticMeshCache;
struct SyncContext;

// SceneSlotSynchronizer::Sync()のフレーム分割ジョブが現在どの段階にあるか。
// 進捗オーバーレイのラベル切替に使う(LoadProgressOverlay.cpp)。
enum class LoadJobPhase {
    Prefetch, // Sceneryプラグインのファイルロード並列プリフェッチ
    Pass1,    // 完全一致revive判定(全entity走査)
    Pass2,    // フォルダ一致フォールバック/新規LoadFolder
    Finalize, // Trainバッチ化判定+残りのGPUアップロード
};

// シーン常駐スロットの所有境界。同期本体の移設中もFacadeから同じSlot集合を公開する。
//
// Sync()はレイアウト読込・Scene切替直後の大規模差分をフレーム分割ジョブとして複数
// フレームにまたがって進める。
// ジョブが進行中の間、呼び出し側(AppRuntimeFrameSim.cpp等)はJobActive()を見て
// シミュレーション/編集入力/Undo/Scene切替/自動復元を一時停止すること(このクラス
// 自身は一時停止の実施箇所ではなく、進行状態を公開するだけ)。
class SceneSlotSynchronizer {
public:
    std::vector<SceneSlotPool::Slot>& Slots() { return slotPool_.Slots(); }
    const std::vector<SceneSlotPool::Slot>& Slots() const { return slotPool_.Slots(); }
    int FindSlotByEntityId(const rs::world::Guid& entityId) const { return slotPool_.FindSlotByEntityId(entityId); }
    const SceneSlotPool& SlotPool() const { return slotPool_; }
    bool ConsumeInitialCarBatchTick() {
        const bool required = requiresInitialCarBatchTick_;
        requiresInitialCarBatchTick_ = false;
        return required;
    }
    // externalMeshesに登録のある配置物は、そのメッシュを組立メッシュとして読み込む(ExternalSceneMeshes.h)。
    void Sync(App& app, const rs::world::SceneRegistry& registry, StaticMeshCache& staticMeshCache,
              const ExternalSceneMeshes& externalMeshes, std::vector<std::string>& log);

    // フレーム分割ジョブが進行中か(読込中オーバーレイ表示・一時停止判定に使う)。
    bool JobActive() const { return loadJob_.active; }
    // 現在のフェーズ(進捗表示のラベル用)。
    LoadJobPhase JobPhase() const { return loadJob_.phase; }
    // ジョブ全体の進み具合(0〜1)。フェーズごとの重みで1本につなぐので、フェーズが変わっても戻らない。
    double JobOverallFraction() const;

private:
    size_t JobPhaseDone() const;
    size_t JobPhaseTotal() const;
    using Slot = SceneSlotPool::Slot;
    SceneSlotPool slotPool_;
    uint64_t pluginGeneration_ = 0;
    uint64_t lastSyncedRevision_ = UINT64_MAX;
    bool requiresInitialCarBatchTick_ = false;
    // テクスチャの事前ロード対象(contentKey+予算ごと)。同じ内容の配置が多いレイアウトで、同じ列挙を
    // スロットの数だけ繰り返さないため(1回が数百ミリ秒かかる)。プラグインの読込の世代が変わったら捨てる。
    std::unordered_map<std::string, PreloadTextureSet> preloadCache_;
    // 非Trainのスロットは内容(フォルダ+スイッチ値)だけで決まるので使い回す。Trainは車両ごとの解決が絡むので毎回求める。
    PreloadTextureSet CollectPreloadTexturesCached(const Slot& slot, size_t userSwitchBudget);

    // パス2(ResolveRemaining相当)で見つかったTrainカテゴリのアップロードを
    // ジョブ完了までここへ蓄積する(バッチ化判定=DeferBatchableCarUploadsは全件が
    // 揃わないと正しく機能しないため、非Trainのように毎チャンクではアップロード
    // しない)。SyncContext::pendingUploads(PendingUpload)と同じ形だが、
    // PendingUpload自体はSceneSlotSyncInternal.h(このヘッダから見て内部専用ヘッダ、
    // SceneSlotSynchronizer.hを直接includeし返す循環関係にある)でのみ定義されるため、
    // フィールドを複製したこの型で保持し、.cpp側でPendingUploadへ詰め替える。
    struct DeferredCarUpload {
        size_t slotIndex = 0;
        rs::world::Guid entityId = rs::world::NilGuid();
        std::string folderUtf8;
        std::string geometryKey;
    };

    // フレーム分割ジョブの状態。Sync()呼び出しをまたいで持ち越す(単一ジョブのみ、
    // 多重開始はSync()側でtargetRevisionの不一致検知により再開始扱いにする)。
    struct LoadJobState {
        bool active = false;
        LoadJobPhase phase = LoadJobPhase::Pass1;
        uint64_t targetRevision = 0;
        // Prefetch: StartLoadJobが要求した対象の出どころ一覧(distinct)。StepPrefetchが
        // ParsedPluginCache::Contains()をこの一覧に対して数えることでprefetchDoneを
        // 求める(entries_全体のサイズを使うと他経路の書き込みと衝突しうるため)。
        std::vector<PluginSource> prefetchSources;
        size_t prefetchTotal = 0;
        size_t prefetchDone = 0;
        // ジョブ開始時にPrefetchから始めたか。始めていなければ全体の進み具合からPrefetchの重みを外す。
        bool startedWithPrefetch = false;
        // パス1: ジョブ開始時点のregistry.Entities()へのインデックスで進行位置を持つ
        // (id文字列化は不要。Sync()冒頭のtargetRevision不一致検知により、ジョブ実行中は
        // registry.Entities()が安定である前提が成り立っている)。pass1Totalはジョブ開始時の
        // 要素数を固定で保持する(進行中に配列が伸びないという上記前提と対をなす)。
        size_t pass1Index = 0;
        size_t pass1Total = 0;
        // パス2: registry内SceneEntityへのポインタではなくid文字列で持つ(フレームを
        // またぐ間にregistryの内部vectorが再確保されうるため、参照のたびFindEntityで
        // 引き直す)。パス1で見つかった順にその場で追記される。
        std::vector<rs::world::Guid> unresolvedIds;
        size_t nextIndex = 0;
        // パス2完了時点ではTrainの全アップロード候補、Finalize移行後は
        // DeferBatchableCarUploadsでバッチ化分を除いた「個別アップロードが必要な
        // 残り」を保持するよう使い回す(finalizeIndexで順にスライス処理する)。
        std::vector<DeferredCarUpload> carPendingUploads;
        size_t finalizeIndex = 0;
        // DeferBatchableCarUploadsはrequiresInitialCarBatchTick_を直接立てるが、
        // Finalizeが複数フレームにまたがる間はまだアップロードが終わっていないため
        // 公開できない。ここへ一旦取っておき、Finalize完了時にのみ公開する
        // (公開が早いと、未アップロードのTrainスロットに対してSceneTrainController側が
        // 同フレームでバッチtickを走らせ、GPUがまだ書き込み中のリソースを参照する
        // 実障害のパターンを招く)。
        bool pendingInitialCarBatchTick = false;
        std::chrono::steady_clock::time_point startTime;
    };
    LoadJobState loadJob_;

    void StartLoadJob(SyncContext& ctx);
    void ParkStaleSlots(SyncContext& ctx);
    // 予算(kSceneLoadFrameBudgetMs)内でPass1→Pass2→Finalizeを連続して進める
    // (フェーズ境界で強制的にフレームを返さない)。最低1件は必ず処理する。
    void RunLoadJobBudget(SyncContext& ctx, std::chrono::steady_clock::time_point budgetStart);
    // 現在のphaseに応じて1単位(Pass1/Pass2は1entity、Finalizeは
    // kSceneSlotUploadsPerSubmission件程度のアップロードスライス)だけ処理を進める。
    void StepLoadJobOnce(SyncContext& ctx);
    // Prefetchの1ステップ: 完了ワーカー結果をParsedPluginCacheへマージし、
    // 全distinct folder分がentries_へ反映されたらPass1/Pass2へ遷移する。非ブロッキング
    // (RunLoadJobBudget側がこのフェーズに限り busy-spin 防止のため即座にループを抜ける)。
    void StepPrefetch(SyncContext& ctx);
    void StepPass1(SyncContext& ctx);
    void ResolveExactReviveOne(SyncContext& ctx, const rs::world::SceneEntity& entity);
    void StepPass2(SyncContext& ctx);
    void ResolveOneEntity(SyncContext& ctx, const rs::world::SceneEntity& entity);
    // Pass2完了→Finalize移行。DeferBatchableCarUploadsを1回だけ実行して残りの
    // 個別アップロード対象を確定させる(空ならこの中でジョブ完了まで進める)。
    void TransitionToFinalize(SyncContext& ctx);
    void StepFinalize(SyncContext& ctx);
    void DeferBatchableCarUploads(SyncContext& ctx);
    void RunPendingUploads(SyncContext& ctx);
    // ctx.pendingUploads(非Train用の累積バッファ)がkSceneSlotUploadsPerSubmission件
    // 溜まっていればアップロードしてクリアする。溜まっていなければ何もしない。
    void FlushNonCarUploadsIfFull(SyncContext& ctx);
    // ctx.pendingUploadsに残りがあれば件数に関わらずアップロードしてクリアする
    // (Sync()から抜ける前・Finalize移行前など、非Trainスロットをアップロード
    // 未済のまま残さないために呼ぶ)。
    void FlushNonCarUploads(SyncContext& ctx);
    // ジョブ完了処理: 取っておいたrequiresInitialCarBatchTick_を公開し、
    // lastSyncedRevision_を確定してジョブ状態をリセットする。
    void CompleteLoadJob();
};

} // namespace rs::app

#include "../gpu/App.h"
//
// SceneSlotSynchronizer.cpp
//
#include "SceneSlotSynchronizer.h"
#include "../../platform/EnvironmentVariable.h"
#include "SceneSlotSyncInternal.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <unordered_set>

#include <spdlog/spdlog.h>

namespace rs::app {

void LogSwitchSyncProfile(const SwitchSyncProfile& prof, double syncMs) {
    if (prof.bakeCount == 0 && prof.loadCount == 0 && prof.reviveExact == 0 &&
        prof.reviveFolder == 0 && prof.reviveNew == 0 && prof.staticProbeLoadCount == 0 &&
        prof.staticProbeCacheHitCount == 0) {
        return; // このSyncで更新が0件なら出さない
    }
    spdlog::info("[switchprof] sync={:.2f}ms pass1={:.1f}ms(sceneryProbeLoad={:.1f}ms(x{}) sceneryProbeHit={}) "
                "pass2={:.1f}ms(bake={:.1f}ms(x{}) load={:.1f}ms(x{}) copy={:.1f}ms "
                "ensureCap={:.1f}ms) dedup={:.1f}ms upload={:.1f}ms cacheHit={} "
                "revive={}/{}/{}(exact/folder/new) meshSkip={}(rebind={})",
                syncMs, prof.pass1Ms, prof.staticProbeLoadMs, prof.staticProbeLoadCount,
                prof.staticProbeCacheHitCount,
                prof.pass2Ms, prof.bakeMs, prof.bakeCount, prof.loadMs, prof.loadCount,
                prof.copyMs, prof.ensureCapMs, prof.dedupMs, prof.uploadMs, prof.cacheHitCount,
                prof.reviveExact, prof.reviveFolder, prof.reviveNew, prof.meshSkipCount,
                prof.textureRebindCount);
}

size_t SceneSlotSynchronizer::JobPhaseDone() const {
    switch (loadJob_.phase) {
        case LoadJobPhase::Prefetch: return loadJob_.prefetchDone;
        case LoadJobPhase::Pass1: return loadJob_.pass1Index;
        case LoadJobPhase::Pass2: return loadJob_.nextIndex;
        case LoadJobPhase::Finalize: return loadJob_.finalizeIndex;
    }
    return 0;
}

size_t SceneSlotSynchronizer::JobPhaseTotal() const {
    switch (loadJob_.phase) {
        case LoadJobPhase::Prefetch: return loadJob_.prefetchTotal;
        case LoadJobPhase::Pass1: return loadJob_.pass1Total;
        case LoadJobPhase::Pass2: return loadJob_.unresolvedIds.size();
        case LoadJobPhase::Finalize: return loadJob_.carPendingUploads.size();
    }
    return 0;
}

namespace {

// 全体の進み具合でのフェーズごとの重み(LoadJobPhaseの並び順)。大規模レイアウトでの
// おおよその所要時間の比。件数が0のフェーズは通過した時点で重みぶん進む。
constexpr std::array<double, 4> kLoadPhaseWeights = {
    40.0, // Prefetch
    5.0,  // Pass1
    45.0, // Pass2
    10.0, // Finalize
};

} // namespace

double SceneSlotSynchronizer::JobOverallFraction() const {
    if (!loadJob_.active) return 1.0;
    const size_t current = static_cast<size_t>(loadJob_.phase);
    const size_t first = loadJob_.startedWithPrefetch ? 0 : static_cast<size_t>(LoadJobPhase::Pass1);
    double totalWeight = 0.0;
    double doneWeight = 0.0;
    for (size_t i = first; i < kLoadPhaseWeights.size(); ++i) {
        totalWeight += kLoadPhaseWeights[i];
        if (i < current) doneWeight += kLoadPhaseWeights[i];
    }
    const size_t total = JobPhaseTotal();
    const double phaseFraction =
        total > 0 ? static_cast<double>(std::min(JobPhaseDone(), total)) / static_cast<double>(total) : 0.0;
    if (current >= first) doneWeight += kLoadPhaseWeights[current] * phaseFraction;
    return totalWeight > 0.0 ? doneWeight / totalWeight : 1.0;
}

void SceneSlotSynchronizer::ParkStaleSlots(SyncContext& ctx) {
    // 削除/内容変化(stale)検知: スロット割当済みだが、registryに無い、または
    // 同じidのまま中身(folderUtf8/switchOptions/category)が入れ替わったentityを
    // 「保管(park)」する。GPUジオメトリ・テクスチャプール・PluginSessionは
    // 保持したまま非表示にするだけで、破棄(ClearSceneSlot、gpu.WaitForGpu()込み)
    // は行わない(Scene切替のたびに
    // SceneRegistryが「全削除→全追加」される都合上、同じSceneへ戻るだけでも
    // 中身が変わっていない配置が毎回破棄→再ロードされて長時間固まるため)。
    //
    // idの強制一意化はしない: ここでのfolderUtf8/switchOptions/category比較が
    // stale-content検知を担い、Scene切替で「同じidだが別プラグインに差し替わった」
    // ケースもこれで検出できる。
    std::vector<Slot>& slots = ctx.slots;
    for (size_t i = 0; i < slots.size(); ++i) {
        if (!slots[i].used) continue;
        const rs::world::SceneEntity* e = ctx.registry.FindEntity(slots[i].entityId);
        const bool stale = !e || e->folderUtf8 != slots[i].residency.folderUtf8 ||
                           e->definitionFile != slots[i].residency.definitionFile ||
                           e->switchOptions != slots[i].residency.switchOptions ||
                           e->category != slots[i].residency.category;
        if (!stale) continue;

        slotPool_.ForgetEntityFailed(slots[i].entityId);
        ctx.app.SetSceneSlotVisible(static_cast<UINT>(i), false);
        slots[i].used = false;
        slots[i].carSnapshot.carVisible = false;
        slots[i].residency.parked = true;
        slots[i].residency.parkedAtRevision = ctx.syncRevision;
        slotPool_.RegisterParked(i);
    }
    // registryから消えた、または内容(folder/switchOptions等)が変わったentityの失敗記録も忘れる
    // (スロットを持てず記録だけされているケース。上のループはslots側からしか辿れないため別途走査。
    // 内容が変わった個体は、新しい内容で改めて読み込みを試す)。
    const rs::world::SceneRegistry& registry = ctx.registry;
    slotPool_.PruneFailedEntities([&registry](const rs::world::Guid& id, const std::string& failedContentKey) {
        const rs::world::SceneEntity* entity = registry.FindEntity(id);
        return entity != nullptr && MakeSlotContentKey(*entity) == failedContentKey;
    });
}

void SceneSlotSynchronizer::StartLoadJob(SyncContext& ctx) {
    ParkStaleSlots(ctx);

    requiresInitialCarBatchTick_ = false;
    loadJob_ = LoadJobState{};
    loadJob_.targetRevision = ctx.syncRevision;
    loadJob_.pass1Total = ctx.registry.Entities().size();
    loadJob_.pass1Index = 0;

    // Prefetch: Scenery・Car配置のうちParsedPluginCache未登録の出どころ(フォルダ+定義ファイル名)を
    // distinctに集め、Pass1に入る前にワーカースレッドへ並列ロードを投げる
    // (非ブロッキング、StepPrefetchが完了をポーリングする)。CarもPass2で同じキーの
    // LoadIntoを通るので、ここで解析しておけばメインスレッドでの直列読込が無くなる。
    std::vector<PluginSource> newScenerSources;
    {
        std::unordered_set<std::string> seen;
        for (const rs::world::SceneEntity& e : ctx.registry.Entities()) {
            if (e.category != rs::world::SceneEntityCategory::Scenery &&
                e.category != rs::world::SceneEntityCategory::Car) continue;
            const PluginSource source(e.folderUtf8, e.definitionFile);
            if (ctx.staticMeshCache.ParsedPlugins().Contains(source)) continue;
            if (!seen.insert(MakePluginSourceKey(source)).second) continue;
            newScenerSources.push_back(source);
        }
    }

    // registryが空(レイアウト読込が次フレームへ遅延しておりまだ1件も投入されて
    // いない等)の場合、StepPass1はentities[0]への添字アクセスを前提にしており
    // pass1Total==0では境界チェック無しに範囲外アクセスするため、ここでPass1自体を
    // 素通りする(--openlayoutのみ・--capture-png無指定でレイアウト読込が
    // frame1へ遅延する経路では、frame0のSync()が空registryのままジョブを開始する)。
    if (!newScenerSources.empty()) {
        ctx.staticMeshCache.ParsedPlugins().RequestPrefetch(newScenerSources);
        loadJob_.prefetchSources = std::move(newScenerSources);
        loadJob_.prefetchTotal = loadJob_.prefetchSources.size();
        loadJob_.prefetchDone = 0;
        loadJob_.phase = LoadJobPhase::Prefetch;
        loadJob_.startedWithPrefetch = true;
    } else {
        loadJob_.phase = loadJob_.pass1Total > 0 ? LoadJobPhase::Pass1 : LoadJobPhase::Pass2;
        loadJob_.startedWithPrefetch = false;
    }
    loadJob_.startTime = std::chrono::steady_clock::now();
    loadJob_.active = true;
}

void SceneSlotSynchronizer::StepLoadJobOnce(SyncContext& ctx) {
    switch (loadJob_.phase) {
        case LoadJobPhase::Prefetch: StepPrefetch(ctx); return;
        case LoadJobPhase::Pass1: StepPass1(ctx); return;
        case LoadJobPhase::Pass2: StepPass2(ctx); return;
        case LoadJobPhase::Finalize: StepFinalize(ctx); return;
    }
}

void SceneSlotSynchronizer::FlushNonCarUploadsIfFull(SyncContext& ctx) {
    if (ctx.pendingUploads.size() < kSceneSlotUploadsPerSubmission) return;
    RunPendingUploads(ctx);
    ctx.pendingUploads.clear();
}

void SceneSlotSynchronizer::FlushNonCarUploads(SyncContext& ctx) {
    if (ctx.pendingUploads.empty()) return;
    RunPendingUploads(ctx);
    ctx.pendingUploads.clear();
}

void SceneSlotSynchronizer::CompleteLoadJob() {
    // Finalize完了まで取っておいたバッチtick要求フラグをここで初めて公開する
    // (LoadJobStateのpendingInitialCarBatchTickコメント参照)。
    requiresInitialCarBatchTick_ = loadJob_.pendingInitialCarBatchTick;
    lastSyncedRevision_ = loadJob_.targetRevision;
    loadJob_ = LoadJobState{};
}

// 1回のSync()呼び出しで、時間予算が残っている限りPass1→Pass2→Finalizeを連続して
// 処理する(フェーズ境界で強制的にフレームを返さない)。小さい編集(スロット数件
// 程度の変更)は1回のSync()呼び出しで完結させ、JobActive()が一瞬trueになって
// 画面がちらつく・sim/編集入力が一時停止することを避ける。
void SceneSlotSynchronizer::RunLoadJobBudget(SyncContext& ctx,
                                             std::chrono::steady_clock::time_point budgetStart) {
    using ProfClock = std::chrono::steady_clock;
    for (;;) {
        StepLoadJobOnce(ctx);
        FlushNonCarUploadsIfFull(ctx);
        if (!loadJob_.active) break; // このSync()呼び出し内でジョブが完了した
        // Prefetchはワーカースレッドの完了待ちであり、メインスレッド側でスピンしても
        // 進捗は進まない(busy-spin防止)。「最低1件は処理する」不変条件もこのフェーズに
        // 限り適用しない。
        if (loadJob_.phase == LoadJobPhase::Prefetch) break;
        const double elapsedMs =
            std::chrono::duration<double, std::milli>(ProfClock::now() - budgetStart).count();
        if (elapsedMs >= kSceneLoadFrameBudgetMs) break;
    }
    // どの経路で抜ける場合も、非Trainスロットをアップロード未済のまま残さない
    // (usedだがGPUメッシュ無しのスロットを翌フレームへ持ち越さない不変条件)。
    FlushNonCarUploads(ctx);
}

void SceneSlotSynchronizer::Sync(App& app, const rs::world::SceneRegistry& registry,
                                 StaticMeshCache& staticMeshCache,
                                 const ExternalSceneMeshes& externalMeshes,
                                 std::vector<std::string>& log) {
    const uint64_t generation = staticMeshCache.Generation();
    if (pluginGeneration_ != generation) {
        // 保管中のセッションも同じ読込結果から派生するため、復活の対象から外す。
        for (size_t index = 0; index < slotPool_.Slots().size(); ++index)
            app.ClearSceneSlot(static_cast<UINT>(index));
        slotPool_.InvalidatePlugins();
        loadJob_ = LoadJobState{};
        lastSyncedRevision_ = UINT64_MAX;
        pluginGeneration_ = generation;
    }
    // [switchprof] 計測専用(RS_PROFILE_LOAD指定時のみ)。getenvは1回だけ読む。
    static const bool kProfileSwitch = rs::platform::ReadEnvironmentVariable("RS_PROFILE_LOAD") != nullptr;
    using ProfClock = std::chrono::steady_clock;
    const ProfClock::time_point tFrame0 = ProfClock::now();

    // 読込ジョブ実行中にregistryが変化した場合(想定外: 呼び出し側は
    // JobActive()の間、registryを変更しうる操作を一時停止する約束事)。ダングリング
    // したunresolvedIds/carPendingUploadsを抱えたまま処理を続けると壊れるため、
    // 安全側に倒して現在のregistry構成でジョブを取り直す。
    if (loadJob_.active && registry.Revision() != loadJob_.targetRevision) {
        spdlog::warn("[scene] 読込ジョブ実行中にレイアウト構成が変化したため、ジョブを再開始します"
                    "(target={} actual={})",
                    loadJob_.targetRevision, registry.Revision());
        loadJob_ = LoadJobState{};
    }

    std::vector<Slot>& slots = slotPool_.Slots();
    SwitchSyncProfile switchProf;
    // このSync()呼び出しぶんの非Trainアップロード累積バッファ(kSceneSlotUploadsPer
    // Submission件溜まるごとに随時フラッシュする、Trainはloadジョブ側で別途蓄積)。
    std::vector<PendingUpload> pendingUploads;

    if (!loadJob_.active) {
        // registryの構成が前回Sync完了時から変化していなければ何もしない(毎フレーム
        // 呼ばれても差分が無ければO(1)。大規模レイアウトでエンティティ数×
        // スロット数のO(N)走査を毎フレーム避けるため)。
        if (registry.Revision() == lastSyncedRevision_) return;

        SyncContext startCtx{app,      registry,       staticMeshCache, externalMeshes, log,
                             slots,    registry.Revision(), kProfileSwitch,  switchProf,
                             pendingUploads};
        StartLoadJob(startCtx);
    }

    SyncContext ctx{app,      registry, staticMeshCache, externalMeshes, log,
                    slots,    loadJob_.targetRevision, kProfileSwitch, switchProf,
                    pendingUploads};
    RunLoadJobBudget(ctx, tFrame0);

    if (kProfileSwitch)
        LogSwitchSyncProfile(
            switchProf,
            std::chrono::duration<double, std::milli>(ProfClock::now() - tFrame0).count());
}

} // namespace rs::app

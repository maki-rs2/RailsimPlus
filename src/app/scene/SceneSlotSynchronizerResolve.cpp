#include "../gpu/App.h"
//
// SceneSlotSynchronizerResolve.cpp
//
// SceneSlotSynchronizer::Sync()のパス1(完全一致revive)・パス2(フォルダ一致
// フォールバック/新規ロード)を1entityずつ進める(RunLoadJobBudgetが時間予算内で
// 繰り返し呼ぶ)。ステージ順序(パス1を全entity分完了させてからパス2に入る)を
// 変えないこと(理由は後述のResolveExactReviveOneコメント参照)。
#include "SceneSlotSynchronizer.h"
#include "SceneSlotSyncInternal.h"

#include <algorithm>
#include <chrono>

#include "../../plugins/schema/SwitchExpr.h"

#include <spdlog/spdlog.h>

namespace rs::app {

// Prefetchの1ステップ。完了したワーカー結果をParsedPluginCacheへ
// マージし、StartLoadJobが要求した対象の出どころ一覧(loadJob_.prefetchSources)が
// 全てentries_へ反映されたらPass1/Pass2へ遷移する。entries_全体のサイズではなく
// このジョブが要求した一覧に対してContains()を数えるのは、他経路(TryGetStatic
// InstanceMesh等)が同時にentries_へ書き込むケースと衝突しないようにするため。
void SceneSlotSynchronizer::StepPrefetch(SyncContext& ctx) {
    ctx.staticMeshCache.ParsedPlugins().HarvestCompletedPrefetch();

    size_t done = 0;
    for (const PluginSource& source : loadJob_.prefetchSources) {
        if (ctx.staticMeshCache.ParsedPlugins().Contains(source)) ++done;
    }
    loadJob_.prefetchDone = done;

    if (loadJob_.prefetchDone >= loadJob_.prefetchTotal) {
        loadJob_.phase = loadJob_.pass1Total > 0 ? LoadJobPhase::Pass1 : LoadJobPhase::Pass2;
    }
}

void SceneSlotSynchronizer::StepPass1(SyncContext& ctx) {
    using ProfClock = std::chrono::steady_clock;
    const ProfClock::time_point t0 = ProfClock::now();

    const rs::world::SceneEntity& entity = ctx.registry.Entities()[loadJob_.pass1Index];
    ResolveExactReviveOne(ctx, entity);
    ++loadJob_.pass1Index;

    if (ctx.profileEnabled) {
        ctx.profile.pass1Ms += std::chrono::duration<double, std::milli>(ProfClock::now() - t0).count();
        ctx.profile.staticProbeLoadMs += ctx.staticMeshCache.TakeProbeLoadMs();
        ctx.profile.staticProbeLoadCount += ctx.staticMeshCache.TakeProbeLoadCount();
        ctx.profile.staticProbeCacheHitCount += ctx.staticMeshCache.TakeProbeCacheHitCount();
    }

    if (loadJob_.pass1Index >= loadJob_.pass1Total) {
        loadJob_.phase = LoadJobPhase::Pass2;
    }
}

// entity1件ぶんの完全一致revive判定。unresolvedなentityはここで
// 直接loadJob_.unresolvedIdsへ追記する(見つかった順=パス2での処理順、全件が
// 出揃うのを待たない)。
//
// パス1をパス2より先に「全entity分」完了させる理由: 1パスに混ぜてしまうと、
// 先に処理されたentityがfolder一致で「後のentityが完全一致でrevive出来たはずの
// parkedスロット」を先取りしてしまい、無料のrevive(ロード・アップロード共に0件)
// を有料のセッション再利用(bakeのみ発生)や新規LoadFolderに後退させる。2パスに
// 分けることで、まず「完全に無料なrevive」を全件確定させてから、残りだけ
// フォールバックを試みる。
void SceneSlotSynchronizer::ResolveExactReviveOne(SyncContext& ctx, const rs::world::SceneEntity& entity) {
    std::vector<Slot>& slots = ctx.slots;

    // 静的Scenery(スイッチ/可動部/時間経過アニメいずれも無し)はGPUインスタンス
    // バッチ側(EditorViewport.cppのneedRebuild時に一括反映)が描画を担当する。
    // ここでスロットを割り当てると二重描画+スロット浪費になるため素通りする
    // (大規模レイアウトで個別配置が数百〜千件規模になるとスロット上限を超えるため)。
    // この判定を下のFindSlotByEntityId/failedEntityIdsチェックより先に行う:
    // 静的インスタンス化対象のSceneryは常にスロットを持たない不変があるため、
    // O(スロット数)のFindSlotByEntityIdを先に全件へ叩くのは無駄(大規模レイアウト
    // 規模ではScenery1万件超に対し毎リビジョン数百万回の文字列比較になる)。
    if (entity.category == rs::world::SceneEntityCategory::Scenery &&
        rs::world::MayUseStaticInstancing(entity)) {
        const rs::render::Mesh* instancedMesh = nullptr;
        if (ctx.staticMeshCache.TryGetStaticInstanceMesh(entity.folderUtf8, entity.switchOptions, ctx.log,
                                     &instancedMesh, entity.isSignalModel, entity.definitionFile))
            return;
    }
    if (FindSlotByEntityId(entity.guid) >= 0) return;
    if (slotPool_.IsEntityFailed(entity.guid)) return;

    const std::string contentKey = MakeSlotContentKey(entity);

    // 復活(revive)経路: 同じ内容(folderUtf8+definitionFile+switchOptions+category)のparkedスロットが
    // あれば、LoadFolder/ApplySwitchOptionsAndBake/GPUアップロードを全てスキップして
    // そのまま使い回す(session/GPUジオメトリ・テクスチャプールは保管時のまま)。
    const int revived = slotPool_.TakeParkedSlot(entity.category, contentKey);
    if (revived >= 0) {
        if (ctx.profileEnabled) ++ctx.profile.reviveExact;
        Slot& s = slots[static_cast<size_t>(revived)];
        s.used = true;
        s.entityId = entity.guid;
        s.residency.AssignContent(entity, contentKey);
        s.carSnapshot.carVisible = true;
        // 復活直後に前回のcachedEntityWorld/partWorldsDirty=falseのまま
        // Tick()へ入ると、entityWorldが偶然一致した場合に再計算をスキップして
        // 保管前のパーツ配置を引きずる。そのため必ずdirty=trueへ戻す。
        s.partWorldsDirty = true;
        // 音源の位置も保管前の配置のものなので、次のTickで求め直すまで鳴らさない。
        s.session.soundEffectStates.assign(s.session.soundEffectAttachments.size(),
                                           SoundEffectRuntimeState{});
        s.session.headlightStates.assign(s.session.headlightAttachments.size(), HeadlightRuntimeState{});
        if (entity.category == rs::world::SceneEntityCategory::Car) {
            // Sceneに再び映るようになった瞬間は車両中心が無関係な座標系へ飛ぶため、
            // 振り子Tiltの位置履歴を捨てる(TrainVehicle.h TiltDriverState規約、
            // TickTrainGroupsの非表示→表示復帰時と同じ扱い)。trainTemplate/
            // axleSpinのサイズはfolderUtf8が同じ=同一プラグインのため再構築不要。
            s.carSnapshot.tiltState.valid = false;
        }
        ctx.app.SetSceneSlotVisible(static_cast<UINT>(revived), true);
        return;
    }

    loadJob_.unresolvedIds.push_back(entity.guid);
}

void SceneSlotSynchronizer::StepPass2(SyncContext& ctx) {
    using ProfClock = std::chrono::steady_clock;
    const ProfClock::time_point t0 = ProfClock::now();

    if (loadJob_.nextIndex < loadJob_.unresolvedIds.size()) {
        const rs::world::Guid& entityId = loadJob_.unresolvedIds[loadJob_.nextIndex];
        ++loadJob_.nextIndex;
        // idで引き直す(フレームをまたぐ間にregistryの内部vectorが再確保されうる
        // ため、ポインタは保持しない。JobActive()の間はregistry変更が一時停止される
        // 前提のため通常はここでnullptrにならないが、想定外に消えていた場合は
        // 静かにスキップする=Sync()冒頭のtargetRevision不一致検知が次フレームで
        // ジョブを取り直す)。
        const rs::world::SceneEntity* entityPtr = ctx.registry.FindEntity(entityId);
        if (entityPtr != nullptr) {
            const size_t beforeSize = ctx.pendingUploads.size();
            ResolveOneEntity(ctx, *entityPtr);
            if (ctx.pendingUploads.size() > beforeSize) {
                // ResolveOneEntityが積んだ1件をTrain/非Trainで振り分ける。非Train
                // はctx.pendingUploadsへ戻し、kSceneSlotUploadsPerSubmission件
                // 溜まった時点でRunLoadJobBudget側が随時フラッシュする(Trainは
                // バッチ化判定が全件揃わないと機能しないためジョブ完了まで蓄積)。
                const PendingUpload pu = ctx.pendingUploads.back();
                ctx.pendingUploads.pop_back();
                if (ctx.slots[pu.slotIndex].isTrainCar) {
                    loadJob_.carPendingUploads.push_back(
                        {pu.slotIndex, pu.entityId, pu.folderUtf8, pu.geometryKey});
                } else {
                    ctx.pendingUploads.push_back(pu);
                }
            }
        }
    }

    if (ctx.profileEnabled)
        ctx.profile.pass2Ms += std::chrono::duration<double, std::milli>(ProfClock::now() - t0).count();

    if (loadJob_.nextIndex >= loadJob_.unresolvedIds.size()) {
        // 非Trainスロットをアップロード未済のまま次のフェーズへ持ち越さない
        // (TransitionToFinalizeがctx.pendingUploadsをTrain用に使い回すため)。
        FlushNonCarUploads(ctx);
        TransitionToFinalize(ctx);
    }
}

namespace {

// 空きスロット/evict/新規成長で確保したスロットへ、プラグインを読み込んでスイッチを焼き込む
// (reusedSessionでない経路)。読込に失敗した(または組立メッシュが無い)ときはログを積み、
// sessionを空へ戻してfalseを返す。後始末(スロット除去・失敗記録)は呼び出し側が行う。
bool LoadAndBakeNewSession(SyncContext& ctx, SceneSlotPool::Slot& s,
                           const rs::world::SceneEntity& entity) {
    using ProfClock = std::chrono::steady_clock;
    if (ctx.profileEnabled) ++ctx.profile.reviveNew;
    const ProfClock::time_point tLoad0 = ProfClock::now();
    const bool cacheHit = ctx.staticMeshCache.ParsedPlugins().LoadInto(
        s.session, PluginSource(entity.folderUtf8, entity.definitionFile), ctx.log);
    // 外から表示メッシュを渡された配置物(押し出し表示など)は、そのメッシュを組立メッシュとして使う。
    if (const std::shared_ptr<const rs::render::Mesh> external = ctx.externalMeshes.Find(entity.guid)) {
        rs::legacy_plugin::AdoptMeshAsAssembly(s.session.plugin, *external);
    }
    if (ctx.profileEnabled && cacheHit) {
        ctx.profile.copyMs +=
            std::chrono::duration<double, std::milli>(ProfClock::now() - tLoad0).count();
        ++ctx.profile.cacheHitCount;
    } else if (ctx.profileEnabled) {
        ctx.profile.loadMs +=
            std::chrono::duration<double, std::milli>(ProfClock::now() - tLoad0).count();
        ++ctx.profile.loadCount;
    }
    if (!s.session.plugin.ok || !s.session.plugin.hasAssembly) {
        ctx.log.push_back("[scene] プラグイン読込失敗、または組立メッシュ無し: " +
                      entity.folderUtf8);
        s.session = PluginSession{};
        return false;
    }

    // 可動部/時間経過アニメを持つため静的インスタンス化対象から外れたScenery
    // (発車標プラグイン等)も、.rs2のスイッチ確定値は1回だけ焼き込む。
    // TrainもLoadFolder直後にここで1回焼き込む(以降のswitchOptions変更は、
    // フォルダ一致のセッション再利用経路で焼き直す)。
    if (entity.category == rs::world::SceneEntityCategory::Scenery ||
        entity.category == rs::world::SceneEntityCategory::Car) {
        BakeSwitchOptionsWithProfile(ctx, s, entity);
    }
    return true;
}

} // namespace

void SceneSlotSynchronizer::ResolveOneEntity(SyncContext& ctx, const rs::world::SceneEntity& entity) {
    std::vector<Slot>& slots = ctx.slots;
    using ProfClock = std::chrono::steady_clock;
    const std::string contentKey = MakeSlotContentKey(entity);

    int slot = -1;
    bool reusedSession = false;
    {
        // Carも含め、フォルダ一致のparkedスロットがあればセッション
        // (PluginSession、moverRuntimes/animationRuntimesの位相状態込み)を
        // 使い回す。Carを除外すると、配置済み列車の
        // スイッチ変更のたびにLoadFolderからの完全再構築(GPU再アップロード
        // 同期待ちでのフリーズ、可動部位相リセットによる不要なアニメ再生)が
        // 起きてしまう。
        slot = slotPool_.TakeParkedSlotByFolder(entity.category, entity.folderUtf8,
                                                entity.definitionFile);
        if (slot >= 0) {
            reusedSession = true;
            if (ctx.profileEnabled) ++ctx.profile.reviveFolder;
            // GPU側の解放(ClearSceneSlotGeometryOnly)とスイッチの焼き直しは、後段の
            // RebakeReusedSlotがまとめて行う(テクスチャだけが変わるときはメッシュを
            // 残すため、焼き直した結果を見てから解放するか決める)。
        }
    }

    bool grown = false;
    if (slot < 0) {
        // 通常経路: 空きスロット→無ければ最も古いparkedスロットをevict→
        // それも無ければ新規成長、の順で確保する。evictはGPU容量確保
        // (EnsureSceneSlotCapacity)が既に済んでいるスロットを再利用するため、
        // 新規成長よりコストが低い。
        slot = slotPool_.FindFreeSlot();
        if (slot < 0) {
            slot = slotPool_.FindOldestParkedSlot();
            if (slot >= 0) {
                ctx.app.ClearSceneSlot(static_cast<UINT>(slot));
                slotPool_.ReleaseParked(static_cast<size_t>(slot));
                slots[static_cast<size_t>(slot)] = Slot{};
            }
        }
        if (slot < 0) {
            grown = true;
            slot = static_cast<int>(slotPool_.GrowSlot());
        }
    }

    Slot& s = slots[static_cast<size_t>(slot)];
    s.carSnapshot.carVisible = true; // スロット再利用時の残留防止
    s.partWorldsDirty = true; // reuse/新規いずれもTick()の不変スキップを避ける

    bool meshKept = false;
    if (reusedSession) {
        // セッション(ディスク読込済みプラグイン)はそのまま、スイッチだけ焼き直す。
        meshKept = RebakeReusedSlot(ctx, entity, static_cast<size_t>(slot));
    } else if (!LoadAndBakeNewSession(ctx, s, entity)) {
        if (grown) slots.pop_back();
        slotPool_.MarkEntityFailed(entity.guid, contentKey);
        return;
    }

    // TakeParkedSlotByFolder経由(reusedSession)は既にそちら側でgenerationを
    // インクリメント済み(park revive)。それ以外(空きスロット/evict直後の
    // 新規Slot{}/新規成長スロット)を初めて使い始める、この遷移でのみ
    // インクリメントする(二重加算防止)。
    if (!reusedSession) ++s.residency.generation;
    s.used = true;
    s.entityId = entity.guid;
    s.residency.AssignContent(entity, contentKey);
    if (entity.category == rs::world::SceneEntityCategory::Car) {
        s.isTrainCar = true;
        s.hasGpuMesh = false;
        const rs::legacy_plugin::TrainVehicleTemplate newTemplate =
            rs::legacy_plugin::BuildTrainVehicleTemplate(s.session.plugin.def);
        // フォルダ一致でセッションを使い回した場合(reusedSession)、車軸数が
        // 変わらなければ車輪回転位相(axleSpin)を維持する。フォルダが同じ=
        // 同一プラグインなのでtrainTemplate自体は再構築しても値は変わらないが、
        // axleSpinをここで0クリアし直すと、配置済み列車のスイッチ変更のたびに
        // 車輪回転が一瞬止まって見えるため、継続時は前回値を残す。
        const bool axleCountUnchanged =
            reusedSession && s.carSnapshot.axleSpin.size() == newTemplate.vehicle.axles.size();
        s.carSnapshot.trainTemplate = newTemplate;
        s.carSnapshot.carPartBoundsDirty = true;
        s.carSnapshot.carPickValid = false;
        // GPU容量不足でこの後s = Slot{}へ破棄されても、性能値計算
        // (CollectTrainGroups)が使い続けられるようentityId単位で取っておく。
        slotPool_.RecordCarTemplate(entity.guid, s.carSnapshot.trainTemplate);
        if (axleCountUnchanged) {
            s.carSnapshot.tiltState.valid = false;
        } else {
            s.carSnapshot.axleSpin.assign(newTemplate.vehicle.axles.size(), 0.0);
            s.carSnapshot.tiltState = rs::sim::TiltDriverState{};
        }
    }

    bool ensuredCapacity = false;
    if (ctx.profileEnabled) {
        const ProfClock::time_point tEnsure0 = ProfClock::now();
        ensuredCapacity = ctx.app.EnsureSceneSlotCapacity(static_cast<UINT>(slot));
        ctx.profile.ensureCapMs +=
            std::chrono::duration<double, std::milli>(ProfClock::now() - tEnsure0).count();
    } else {
        ensuredCapacity = ctx.app.EnsureSceneSlotCapacity(static_cast<UINT>(slot));
    }
    if (!ensuredCapacity) {
        ctx.log.push_back("[scene] GPU常駐スロットの空きが無いため配置できません: " +
                      entity.folderUtf8);
        s = Slot{};
        if (grown) slots.pop_back();
        slotPool_.MarkEntityFailed(entity.guid, contentKey);
        return;
    }

    // GPUのメッシュを残したスロットは上げ直さない(テクスチャの差し替えは済んでいる)。
    if (meshKept) return;

    // geometryKeyはfolder単独ではなくfolder+スイッチ確定値にする: GeometryManagerは
    // このキーだけでGPU頂点/インデックスバッファを使い回すため、同じfolderでも
    // ApplySwitchOptionsAndBake(ChangeModel)で頂点数が変わる別配置(コンビニの
    // ブランド違い等)がfolder単独キーを共有すると、後から呼ばれた側の
    // vbView_.SizeInBytesが先勝ちの(サイズの異なる)共有バッファを越えて指す
    // 不整合になり、DXGI device lost(DEVICE_HUNG)を起こす
    // (D3D12デバッグレイヤーでは IASetVertexBuffers exceeds end of virtual address
    // range と報告される)。EditorViewport.cppのGPUインスタンスバッチ経路と同じ
    // SceneRenderer::MakeStaticInstanceKeyで揃える。
    ctx.pendingUploads.push_back({static_cast<size_t>(slot), entity.guid, entity.folderUtf8, MakePluginMeshKey(contentKey)});
}

void SceneSlotSynchronizer::TransitionToFinalize(SyncContext& ctx) {
    // ここに来る時点でctx.pendingUploadsは空である前提(StepPass2が直前に
    // FlushNonCarUploadsを強制実行している)。Trainのアップロード候補を積み直して
    // DeferBatchableCarUploadsへそのまま渡す。
    ctx.pendingUploads.clear();
    ctx.pendingUploads.reserve(loadJob_.carPendingUploads.size());
    for (const DeferredCarUpload& deferred : loadJob_.carPendingUploads) {
        ctx.pendingUploads.push_back(
            {deferred.slotIndex, deferred.entityId, deferred.folderUtf8, deferred.geometryKey});
    }

    // 同一content/geometryのTrainが複数ありJoint解決にも問題が無い場合は、直後の
    // Tickで TrainInstanceBatchManager が一度だけGPUへ送る(バッチ化分はここで
    // pendingUploadsから除かれる)。requiresInitialCarBatchTick_はここで直接
    // 立つが、Finalizeが複数フレームにまたがる間は公開せず取っておく
    // (LoadJobState::pendingInitialCarBatchTickコメント参照)。
    DeferBatchableCarUploads(ctx);
    loadJob_.pendingInitialCarBatchTick = requiresInitialCarBatchTick_;
    requiresInitialCarBatchTick_ = false;

    loadJob_.carPendingUploads.clear();
    loadJob_.carPendingUploads.reserve(ctx.pendingUploads.size());
    for (const PendingUpload& pendingUpload : ctx.pendingUploads) {
        loadJob_.carPendingUploads.push_back({pendingUpload.slotIndex, pendingUpload.entityId,
                                              pendingUpload.folderUtf8, pendingUpload.geometryKey});
    }
    ctx.pendingUploads.clear();

    loadJob_.finalizeIndex = 0;
    if (loadJob_.carPendingUploads.empty()) {
        CompleteLoadJob();
    } else {
        loadJob_.phase = LoadJobPhase::Finalize;
    }
}

void SceneSlotSynchronizer::StepFinalize(SyncContext& ctx) {
    const size_t total = loadJob_.carPendingUploads.size();
    const size_t begin = loadJob_.finalizeIndex;
    const size_t end = std::min(begin + kSceneSlotUploadsPerSubmission, total);

    ctx.pendingUploads.clear();
    ctx.pendingUploads.reserve(end - begin);
    for (size_t i = begin; i < end; ++i) {
        const DeferredCarUpload& deferred = loadJob_.carPendingUploads[i];
        ctx.pendingUploads.push_back(
            {deferred.slotIndex, deferred.entityId, deferred.folderUtf8, deferred.geometryKey});
    }
    RunPendingUploads(ctx);
    ctx.pendingUploads.clear();

    loadJob_.finalizeIndex = end;
    if (loadJob_.finalizeIndex >= total) CompleteLoadJob();
}

} // namespace rs::app

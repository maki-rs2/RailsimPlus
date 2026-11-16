#include "ViewportPlacementInstances.h"

#include "../../platform/EnvironmentVariable.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <spdlog/spdlog.h>

#include "../../core/Math.h"
#include "../../editor/tool/Editor.h"
#include "../../world/scene/ScenePlacement.h"
#include "../../world/scene/SceneRegistry.h"
#include "../../render/resource/AsyncLoader.h"
#include "../gpu/App.h"
#include "../scene/ManualPlacementOverlayApply.h"
#include "../scene/TrackPluginPlacement.h"
#include "../scene/SceneRenderer.h"
#include "ViewportPierBatchPlan.h"
#include "ViewportSceneryInstances.h"

namespace rs::app {

namespace {

constexpr size_t kPlacementUploadBatchesPerSubmission = 8;

using ProfClock = std::chrono::steady_clock;

// item配列と同じ順のバッチ割当て(AddInstance側で再利用する)と、計測用の所要。
struct TrackPluginBatchAssignment {
    // itemごとのバッチキー。空キー=バッチ非対象(後段でスキップ)。
    std::vector<std::string> itemBatchKeys;
    // Pier(橋脚)はローカル原点(y=0)がシャフト下端(地面設置面)なので、
    // 配置時は取付点(レール高さ)ではなく地面(world y=0)へ置く必要がある
    // (AddInstance時にY平行移動を0に潰す。シャフト高さを実高さに連動させたうえで
    // 取付点にy=0を置くと、橋脚がレールからさらに上へ突き出す)。
    std::vector<bool> itemIsPier;
    double findMs = 0.0;
    double worldMs = 0.0;
};

// 自動配置のitemごとにバッチキーを決め、新規バッチに必要なassemblyMeshをpendingへ集める。
// folderUtf8ごとのLoadFolder結果はctx.sceneRenderer側でセッションを跨いでキャッシュされる
// (TryGetTrackPluginAssemblyMesh)ので、編集のたびにこの処理が走っても読込済みのものは
// ディスクから読み直さない。
// Pier(橋脚)はfolderUtf8単独キーではなく「folderUtf8+実際の設置点高さ
// (地面y=0からレール取付点までの距離、0.25m単位に丸め)」をキーにする。
// シャフト高さを固定値にすると橋脚が地面まで届かず宙に浮くため、設置点の
// 高さをキーに含めてアセンブリを作り分ける必要がある(地形メッシュの概念が
// 無くこのエンジンの地面は常にy=0なので、シャフト高さ=設置点のワールドY座標
// でよい)。Pier以外(Pole等)はfolderUtf8単独キーのまま。
void AssignTrackPluginBatches(const EditorViewportContext& ctx,
                              const std::vector<TrackPluginPlacementItem>& trackPluginItems,
                              std::unordered_set<std::string>& trackPluginSeen,
                              std::vector<PendingInstanceBatch>& pending,
                              std::unordered_map<std::string, bool>& batchOk,
                              TrackPluginBatchAssignment& out) {
    rs::editor::Editor& gEditor = ctx.editor;
    out.itemBatchKeys.assign(trackPluginItems.size(), std::string());
    out.itemIsPier.assign(trackPluginItems.size(), false);
    for (size_t i = 0; i < trackPluginItems.size(); ++i) {
        const TrackPluginPlacementItem& item = trackPluginItems[i];
        const ProfClock::time_point tFind0 = ProfClock::now();
        const TrackPluginEntry* entry = ctx.selector.FindTrackPluginEntry(item.folderUtf8);
        out.findMs += std::chrono::duration<double, std::milli>(ProfClock::now() - tFind0).count();
        const bool isPier = entry != nullptr && entry->type == rs::legacy_pi::PluginType::Pier;
        out.itemIsPier[i] = isPier;

        if (!isPier) {
            const std::string key = MakePluginMeshKey(MakePluginSourceKey(item.folderUtf8, ""));
            out.itemBatchKeys[i] = key;
            if (!trackPluginSeen.insert(key).second) continue;
            const rs::render::Mesh* mesh = nullptr;
            if (!ctx.sceneRenderer.TryGetTrackPluginAssemblyMesh(item.folderUtf8, ctx.log, &mesh)) {
                ctx.log.push_back("[TrackPlugin][warn] プラグイン読込失敗: " + item.folderUtf8);
                batchOk[key] = false;
                continue;
            }
            pending.push_back({key, mesh});
            continue;
        }

        const ProfClock::time_point tWorld0 = ProfClock::now();
        const rs::core::Mat4 world = rs::world::ResolveWorldMatrix(item.placement, gEditor.graph);
        out.worldMs += std::chrono::duration<double, std::milli>(ProfClock::now() - tWorld0).count();
        const PierBatchPlan plan = ComputePierBatchPlan(item.folderUtf8, entry, world);
        if (plan.skip) {
            out.itemBatchKeys[i].clear(); // 空キー=このitemはバッチ非対象(後段でスキップ)
            continue;
        }
        out.itemBatchKeys[i] = plan.key;
        if (!trackPluginSeen.insert(plan.key).second) continue;
        const rs::render::Mesh* mesh = nullptr;
        if (!ctx.sceneRenderer.TryGetPierAssemblyMesh(item.folderUtf8, plan.roundedHeight, ctx.log,
                                                       &mesh)) {
            ctx.log.push_back("[TrackPlugin][warn] プラグイン読込失敗: " + item.folderUtf8);
            batchOk[plan.key] = false;
            continue;
        }
        pending.push_back({plan.key, mesh});
    }
}

// pendingのバッチをGPUへ登録する。アップロードはチャンク単位で1回のRunUploadCommandsにまとめる。
void UploadPendingBatches(const EditorViewportContext& ctx, const std::vector<PendingInstanceBatch>& pending,
                          std::unordered_map<std::string, bool>& batchOk) {
    if (!pending.empty()) {
        const size_t submissionCount =
            (pending.size() + kPlacementUploadBatchesPerSubmission - 1) /
            kPlacementUploadBatchesPerSubmission;
        if (submissionCount > 1) {
            spdlog::debug("[mesh] splitting {} instance batches into {} upload submission(s)",
                         pending.size(), submissionCount);
        }
        ctx.app.RunUploadCommandsChunked(
            pending.size(), kPlacementUploadBatchesPerSubmission,
            [&](ID3D12Device* device, DirectX::ResourceUploadBatch& upload,
                size_t begin, size_t end) {
                // このサブミッション分(最大kPlacementUploadBatchesPerSubmission件)の
                // テクスチャを先にまとめて並列デコードする。EnsureBatch内のメッシュ単位の
                // 先読みは1バッチ数枚で並列化の効果が出ないため、バッチをまたいで集める。
                // 範囲を広げるとImageCacheの予算を超えて追い出し・再デコードが起きる。
                std::vector<std::string> texturePaths;
                for (size_t i = begin; i < end; ++i) {
                    ctx.app.InstanceBatches().CollectNewBatchTexturePaths(
                        pending[i].key, *pending[i].mesh, texturePaths);
                }
                rs::render::PrefetchImages(texturePaths);

                for (size_t i = begin; i < end; ++i) {
                    const PendingInstanceBatch& p = pending[i];
                    batchOk[p.key] = ctx.app.InstanceBatches().EnsureBatch(
                        device, upload, p.key, *p.mesh);
                }
            });
    }
}

// 静的メッシュキャッシュ/アセンブリキャッシュの切り詰め。
// 静的メッシュキャッシュの切り詰めはここでだけ行う。この地点は取得した
// `const Mesh*`(pending)を使い終わった後なので、破棄しても宙に浮く
// ポインタが生じない(SceneRenderer::PruneStaticMeshCache参照)。今回の
// リビルドで使ったキーはliveとして残し、上限をわずかに超える規模の
// レイアウトで毎回作り直すスラッシングを避ける。
void PruneRebuildCaches(const EditorViewportContext& ctx,
                        const std::unordered_set<std::string>& trackPluginSeen) {
    std::unordered_set<std::string> liveStaticKeys;
    for (const rs::world::SceneEntity& entity : ctx.sceneRegistry.Entities()) {
        if (entity.category != rs::world::SceneEntityCategory::Scenery) continue;
        const TrackPluginEntry* pierEntry = ctx.selector.FindTrackPluginEntry(entity.folderUtf8);
        if (pierEntry != nullptr && pierEntry->type == rs::legacy_pi::PluginType::Pier)
            continue;  // pierAssemblyCache_側で保護される(trackPluginSeen経由)
        liveStaticKeys.insert(rs::app::SceneRenderer::MakeStaticInstanceKey(
            entity.folderUtf8, entity.switchOptions, entity.isSignalModel, entity.definitionFile));
    }
    // 信号プラグインのモデル(SceneRegistry外)のキーも残す。
    for (const rs::world::SceneEntity& entity : ctx.sceneRenderer.SignalModels().Entities()) {
        liveStaticKeys.insert(rs::app::SceneRenderer::MakeStaticInstanceKey(
            entity.folderUtf8, entity.switchOptions, entity.isSignalModel, entity.definitionFile));
    }
    ctx.sceneRenderer.PruneStaticMeshCache(liveStaticKeys);

    // trackPluginAssemblyCache_/pierAssemblyCache_も同様に切り詰める。liveKeysには
    // 上のtrackPluginItemsループで既に構築済みのtrackPluginSeenをそのまま渡す。
    // trackPluginSeenはfolderUtf8単独キー(trackPluginAssemblyCache_用)と
    // folder+丸めた高さの複合キー(pierAssemblyCache_用)が混在した和集合
    // (スーパーセット)だが、これは意図的でバグではない。キー文字列の形式が
    // 構造的に異なる(複合キーは"|"区切りの高さを含む)ため他方のキャッシュの
    // キーと衝突することはなく、「本来evictしてよいエントリを誤って保護し
    // 続ける」方向にしか作用しない(過保護なだけで、生きているはずのエントリを
    // 誤って破棄することは無い)。専用の集合に分割する必要はない。
    ctx.sceneRenderer.PruneTrackPluginAssemblyCache(trackPluginSeen);
    ctx.sceneRenderer.PrunePierAssemblyCache(trackPluginSeen);
}

// 割当て済みのキーへ自動配置のインスタンスを登録する。
void AddTrackPluginInstances(const EditorViewportContext& ctx,
                             const std::vector<TrackPluginPlacementItem>& trackPluginItems,
                             const TrackPluginBatchAssignment& assignment,
                             const std::unordered_map<std::string, bool>& batchOk) {
    rs::editor::Editor& gEditor = ctx.editor;
    for (size_t i = 0; i < trackPluginItems.size(); ++i) {
        const std::string& batchKey = assignment.itemBatchKeys[i];
        if (batchKey.empty()) continue; // buildMinAlt未満でスキップされたPier
        const auto it = batchOk.find(batchKey);
        if (it == batchOk.end() || !it->second) continue;
        const TrackPluginPlacementItem& item = trackPluginItems[i];
        rs::core::Mat4 world = rs::world::ResolveWorldMatrix(item.placement, gEditor.graph);
        // Pierのローカル原点はシャフト下端(地面)なので、Y平行移動は取付点の
        // 高さではなく地面(0)にする(シャフトはローカルy=0→heightMで既に
        // 取付点の高さまで押し出し済み)。
        if (assignment.itemIsPier[i]) ZeroPierWorldY(world);
        ctx.app.InstanceBatches().AddInstance(batchKey, world.m);
    }
}

} // namespace

// 離散配置(Pier/Pole/Scenery)のGPUインスタンスバッチ反映(自動配置+手動オーバーレイ適用後の
// 一覧をInstanceBatchManagerへ登録し、placementCache/ピッキング用キャッシュも更新する)。
void RebuildDiscretePlacementInstances(const EditorViewportContext& ctx) {
    rs::editor::Editor& gEditor = ctx.editor;

    // [switchprof] 計測専用。RS_PROFILE_LOAD
    // 指定時のみ、この関数全体の所要と処理規模を1行で出す。getenvは1回だけ読む。
    static const bool kProfileSwitch = rs::platform::ReadEnvironmentVariable("RS_PROFILE_LOAD") != nullptr;
    const ProfClock::time_point tRebuild0 = ProfClock::now();

    // 離散配置(Pier/Pole): GPUインスタンスバッチへ反映する。SceneRegistry経由の
    // 「1配置=1描画」だと配置数分の個別ドローコール+毎フレームのパートワールド
    // 再計算が発生して重い。RecordOpaqueはFrameRenderer::Render内で毎フレーム
    // 自動的に呼ばれるため、
    // revision変化時に配置(instances)だけ全件再構築する。バッチ自体(basePath単位の
    // GPUジオメトリ/テクスチャ)はClearAll()せず、ClearInstances()+Prune()で
    // 使われなくなったものだけをGCする。ClearAll()にすると無関係な既存バッチまで
    // 含め編集のたびに全ジオメトリ/テクスチャをGPUへ再送することになり、レール
    // 1本の編集でも数百バッチ分の再アップロードが走って固まる。
    // 自動配置(離散=Pier/Pole)を導出し、手動オーバーレイ(個別削除/手動追加)を
    // 後段適用する。
    // [switchprof] 段階別の内訳(計測専用。RS_PROFILE_LOAD指定時のみ意味を持つ)。
    double profCollectMs = 0.0, profMeshMs = 0.0, profUploadMs = 0.0, profAddMs = 0.0;
    const ProfClock::time_point tCollect0 = ProfClock::now();

    std::vector<TrackPluginPlacementItem> rawItems =
        CollectTrackPluginPlacements(ctx.selector.TrackPluginEntries(), gEditor.graph, ctx.log);
    std::vector<TrackPluginPlacementItem> trackPluginItems =
        ApplyOverlayToItems(rawItems, gEditor.manualOverlay);
    profCollectMs = std::chrono::duration<double, std::milli>(ProfClock::now() - tCollect0).count();
    // 個別ピッキング用キャッシュ。rebuild時のみ更新し、他フレームは
    // 直近の内容を保持する(数千件規模でも1クリック/1フレームのO(N)走査で足りる
    // ため、空間索引等は導入しない)。
    ctx.placementCache = trackPluginItems;

    // 新規バッチに必要なassemblyMesh(CPU側)を先に全件揃えてから、GPUアップロードは
    // 最後に1回のRunUploadCommands(Begin/End/waitが1回だけ)へまとめる。
    // 新規バッチ1件ごとにRunUploadCommandsを呼ぶと、バッチ数が多いレイアウト
    // (情景の大量配置。switchOptionsを含むキーはfolder単独よりさらに細分化される)
    // では同期GPU待ちが数百回連続し、1フレームの処理時間がドライバのTDRタイム
    // アウトを超えてDXGI device lost(hung)を起こす。
    using PendingBatch = PendingInstanceBatch;
    std::vector<PendingBatch> pending;
    const ProfClock::time_point tMesh0 = ProfClock::now();
    double profRailLoopMs = 0.0;
    int    profSceneryBatches = 0;
    std::unordered_map<std::string, bool> batchOk; // key -> EnsureBatch成否(未判定分は含まない)

    std::unordered_set<std::string> trackPluginSeen;
    TrackPluginBatchAssignment assignment;
    AssignTrackPluginBatches(ctx, trackPluginItems, trackPluginSeen, pending, batchOk, assignment);


    // 個別配置物(.rs2のStruct/Station/Pole/Pier標準配置、SceneRegistry
    // category==Scenery): 静的判定(PluginSession::IsFullyStatic)を通るものだけ
    // 同じInstanceBatchManagerへ合流させる。合流させないと、大規模レイアウトでは
    // 千件規模の配置の大半がSceneRegistryのスロット上限を超えて非表示になる。
    // スイッチ/可動部/時間経過アニメを持つプラグインは判定を通らずSceneRegistry
    // 側に残る=SceneRenderer::Syncが引き続き処理する。
    // folder単独ではなくfolder+スイッチ確定値をバッチキーにする: 同じフォルダでも
    // .rs2のSwitchOptionが異なれば(コンビニのブランド違い等)見た目が変わるため、
    // 同一バッチへ合流させると常に先勝ちの1状態へ潰れてしまう
    // (PluginSession::ApplySwitchOptionsAndBake参照)。
    profRailLoopMs = std::chrono::duration<double, std::milli>(ProfClock::now() - tMesh0).count();
    // バンド管理(_CAMDIST参照の静的配置)は毎回登録し直す。リビルド後の積み直しは
    // この関数の最後のApplyBandedStaticInstancesで行う。
    ctx.sceneRenderer.BandedStaticInstances().Reset();
    BandedGroupIndexMap bandedGroups;
    CollectSceneryBatches(ctx, trackPluginSeen, pending, bandedGroups, profSceneryBatches);

    // ここまでのpendingが「今回のリビルドで実際に必要なバッチキーの全集合」
    // なので、これを使って不要バッチをGCしてから配置をゼロクリアする
    // (Prune→ClearInstancesの順。ClearInstances()はEnsureBatchで残る/新設
    // されるバッチの配置数を0に戻すだけで、ジオメトリ/テクスチャの再アップロードは
    // 発生しない=既存バッチはEnsureBatch内でno-op)。
    profMeshMs = std::chrono::duration<double, std::milli>(ProfClock::now() - tMesh0).count();
    const ProfClock::time_point tUpload0 = ProfClock::now();
    std::unordered_set<std::string> liveBatchKeys;
    for (const PendingBatch& p : pending) liveBatchKeys.insert(p.key);
    ctx.app.InstanceBatches().Prune(liveBatchKeys);
    ctx.app.InstanceBatches().ClearInstances();

    UploadPendingBatches(ctx, pending, batchOk);

    profUploadMs = std::chrono::duration<double, std::milli>(ProfClock::now() - tUpload0).count();
    const ProfClock::time_point tAdd0 = ProfClock::now();

    AddTrackPluginInstances(ctx, trackPluginItems, assignment, batchOk);
    if (!trackPluginItems.empty()) {
        spdlog::debug("[TrackPlugin] instanced {} placements in {} batch(es)",
                    ctx.app.InstanceBatches().TotalInstanceCount(),
                    ctx.app.InstanceBatches().BatchCount());
    }

    const int sceneryInstancedCount = AddSceneryInstances(ctx, batchOk, bandedGroups);
    if (sceneryInstancedCount > 0) {
        spdlog::debug("[Scenery] instanced {} placements (total batches now {})",
                    sceneryInstancedCount, ctx.app.InstanceBatches().BatchCount());
    }
    // 上で全消去したので、バンド管理の配置をここで積み直す(次のフレームまで待つと
    // _CAMDIST参照の配置が1フレーム消える)。
    ctx.sceneRenderer.ApplyBandedStaticInstances(ctx.app);

    PruneRebuildCaches(ctx, trackPluginSeen);

    if (kProfileSwitch) {
        profAddMs = std::chrono::duration<double, std::milli>(ProfClock::now() - tAdd0).count();
        const double rebuildMs =
            std::chrono::duration<double, std::milli>(ProfClock::now() - tRebuild0).count();
        spdlog::info(
            "[switchprof] instRebuild={:.1f}ms (collect={:.1f} mesh={:.1f}[rail={:.1f} "
            "scenery={:.1f}/keys={}/cache={}] find={:.1f} world={:.1f} upload={:.1f} "
            "add={:.1f}) placements={} batches={}",
            rebuildMs, profCollectMs, profMeshMs, profRailLoopMs, profMeshMs - profRailLoopMs,
            profSceneryBatches, ctx.sceneRenderer.StaticClassCacheSize(), assignment.findMs,
            assignment.worldMs, profUploadMs, profAddMs,
            ctx.app.InstanceBatches().TotalInstanceCount(),
            ctx.app.InstanceBatches().BatchCount());
    }
}

} // namespace rs::app

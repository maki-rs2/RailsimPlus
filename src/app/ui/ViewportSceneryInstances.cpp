#include "ViewportSceneryInstances.h"

#include "../../platform/EnvironmentVariable.h"
#include <chrono>
#include <cstdlib>

#include <spdlog/spdlog.h>

#include "../../editor/tool/Editor.h"
#include "../../render/resource/AsyncLoader.h"
#include "../../world/scene/ScenePlacement.h"
#include "../../world/scene/SceneRegistry.h"
#include "../gpu/App.h"
#include "../scene/SceneRenderer.h"
#include "../scene/StaticInstanceKey.h"
#include "ViewportPierBatchPlan.h"

namespace rs::app {

namespace {

bool IsOrphanPier(const EditorViewportContext& ctx, const rs::world::SceneEntity& entity,
                  const TrackPluginEntry*& outEntry) {
    outEntry = ctx.selector.FindTrackPluginEntry(entity.folderUtf8);
    return outEntry != nullptr && outEntry->type == rs::legacy_pi::PluginType::Pier;
}

bool AllVariantsEmpty(const StaticMeshVariantSet& set) {
    for (const rs::render::Mesh& mesh : set.variants) {
        if (!mesh.Empty()) return false;
    }
    return true;
}

// 静的バッチで描く個体の灯具を、一覧(StaticHeadlightRoster)へ入れる。灯具が無い・全パーツ非表示・
// 静的にできない個体は一覧から外す。スイッチが変わった個体は、新しい組の灯具で入れ直す。
void UpdateHeadlightRoster(const EditorViewportContext& ctx, const rs::world::SceneEntity& entity) {
    const StaticMeshVariantSet* variants = nullptr;
    const bool drawn = ctx.sceneRenderer.TryGetStaticInstanceVariants(entity.folderUtf8, entity.switchOptions,
                                                                      ctx.log, &variants, entity.isSignalModel, entity.definitionFile);
    if (drawn && variants->HasAnyHeadlight() && !AllVariantsEmpty(*variants))
        ctx.sceneRenderer.StaticHeadlights().Set(entity.guid, entity.isSignalModel, *variants);
    else
        ctx.sceneRenderer.StaticHeadlights().Erase(entity.guid);
}

// 非バリアント切り替えのSceneryを、解決済みのバッチキーへ1配置として積む。
// 全再構築(AddSceneryInstances)と差分更新(ApplySwitchChangesToSceneryInstances)で共通。
void AddStaticSceneryInstance(const EditorViewportContext& ctx, const rs::world::SceneEntity& entity,
                              const std::string& batchKey) {
    const rs::core::Mat4 world = rs::world::ResolveWorldMatrix(entity.placement, ctx.editor.graph);
    ctx.app.InstanceBatches().AddInstance(batchKey, world.m);
    UpdateHeadlightRoster(ctx, entity);
}

// 描画対象のScenery系エンティティ全件。SceneRegistryの個体に加え、信号プラグインのモデル
// (SignalModelSet。SceneRegistryには入れない)も同じ経路で積む。
template <typename Fn>
void ForEachDrawnEntity(const EditorViewportContext& ctx, Fn&& fn) {
    for (const rs::world::SceneEntity& entity : ctx.sceneRegistry.Entities()) {
        if (rs::world::MayUseStaticInstancing(entity)) fn(entity); // 一時的な配置物はスロット側だけで描く
    }
    for (const rs::world::SceneEntity& entity : ctx.sceneRenderer.SignalModels().Entities()) fn(entity);
}

const rs::world::SceneEntity* FindDrawnEntity(const EditorViewportContext& ctx, const rs::world::Guid& id) {
    const rs::world::SceneEntity* entity = ctx.sceneRegistry.FindEntity(id);
    return entity != nullptr ? entity : ctx.sceneRenderer.SignalModels().FindEntity(id);
}

} // namespace

void CollectSceneryBatches(const EditorViewportContext& ctx,
                           std::unordered_set<std::string>& trackPluginSeen,
                           std::vector<PendingInstanceBatch>& pending,
                           BandedGroupIndexMap& bandedGroups, int& outBatchCount) {
    // PierLinkに紐付かない孤立Pier(Scenery経路に落ちた個体)は、trackPluginItemsループの
    // Pier分岐と同じ高さ丸め規則(ComputePierBatchPlan)で扱う必要がある
    // (地面基準の固定シャフト高さのまま置くと接合点から上に浮く/埋まる)。ピッキングAABBは固定高さの
    // ままである(SceneSceneryPicker.cpp)。
    std::unordered_set<std::string> scenerySeen;
    // 信号のモデル(isSignalModel)は駅の接近も停車も無いので、駅舎の実行時スイッチを0とみなして静的判定する
    // (TryGetStaticInstanceVariants が個体の印で別に判定する)。
    ForEachDrawnEntity(ctx, [&](const rs::world::SceneEntity& entity) {
        if (entity.category != rs::world::SceneEntityCategory::Scenery) return;
        const TrackPluginEntry* pierEntry = nullptr;
        if (IsOrphanPier(ctx, entity, pierEntry)) {
            const rs::core::Mat4 world = rs::world::ResolveWorldMatrix(entity.placement, ctx.editor.graph);
            const PierBatchPlan plan = ComputePierBatchPlan(entity.folderUtf8, pierEntry, world);
            if (plan.skip) return;
            if (!trackPluginSeen.insert(plan.key).second) return;
            const rs::render::Mesh* mesh = nullptr;
            if (!ctx.sceneRenderer.TryGetPierAssemblyMesh(entity.folderUtf8, plan.roundedHeight,
                                                           ctx.log, &mesh))
                return;
            ++outBatchCount;
            pending.push_back({plan.key, mesh});
            return;
        }
        const std::string baseKey = rs::app::SceneRenderer::MakeStaticInstanceKey(
            entity.folderUtf8, entity.switchOptions, entity.isSignalModel, entity.definitionFile);
        if (!scenerySeen.insert(baseKey).second) return;
        const StaticMeshVariantSet* variants = nullptr;
        if (!ctx.sceneRenderer.TryGetStaticInstanceVariants(entity.folderUtf8, entity.switchOptions,
                                                             ctx.log, &variants, entity.isSignalModel, entity.definitionFile))
            return;
        // _CAMDISTか共有状態(_NIGHT/_SEASON/_SHADOW/_ENVMAP)でバリアントが切り替わる集合は、バッチキーを
        // バリアントごとに分け、配置の割り当てをバリアント管理へ預ける。
        const bool banded = variants->NeedsVariantSwitching() && !AllVariantsEmpty(*variants);
        if (banded) {
            bandedGroups[baseKey] =
                ctx.sceneRenderer.BandedStaticInstances().AddGroup(baseKey, *variants);
        }
        const size_t variantCount = variants->variants.size();
        for (size_t i = 0; i < variantCount; ++i) {
            const rs::render::Mesh& mesh = variants->variants[i];
            // ChangeModel=""(空モデル選択)で全パーツ非表示になるSwitchOption組み合わせや
            // _CAMDISTのバンドや共有状態の状態は、焼き込み後の組立メッシュが空になる。これは正常な非表示であり、
            // EnsureBatchへ渡すとMeshPipeline::UploadMeshがvertices空でfalseを返し、
            // リビルドのたびに"UploadModel failed"警告が出続けるため、バッチ生成自体を
            // スキップする(架線柱パーツで実発生)。
            if (mesh.Empty()) continue;
            ++outBatchCount;
            pending.push_back({MakeStaticVariantKey(baseKey, i, variantCount), &mesh});
        }
    });
}

int AddSceneryInstances(const EditorViewportContext& ctx,
                        const std::unordered_map<std::string, bool>& batchOk,
                        const BandedGroupIndexMap& bandedGroups) {
    int instancedCount = 0;
    ctx.sceneRenderer.StaticHeadlights().Clear();
    ForEachDrawnEntity(ctx, [&](const rs::world::SceneEntity& entity) {
        if (entity.category != rs::world::SceneEntityCategory::Scenery) return;
        const TrackPluginEntry* pierEntry = nullptr;
        const bool isOrphanPier = IsOrphanPier(ctx, entity, pierEntry);
        std::string batchKey;
        if (isOrphanPier) {
            rs::core::Mat4 world = rs::world::ResolveWorldMatrix(entity.placement, ctx.editor.graph);
            const PierBatchPlan plan = ComputePierBatchPlan(entity.folderUtf8, pierEntry, world);
            if (plan.skip) return;
            batchKey = plan.key;
            ZeroPierWorldY(world);
            const std::unordered_map<std::string, bool>::const_iterator pierIt = batchOk.find(batchKey);
            if (pierIt == batchOk.end() || !pierIt->second) return;
            ctx.app.InstanceBatches().AddInstance(batchKey, world.m);
            ++instancedCount;
            return;
        } else {
            batchKey = rs::app::SceneRenderer::MakeStaticInstanceKey(entity.folderUtf8, entity.switchOptions,
                                                                      entity.isSignalModel, entity.definitionFile);
            const BandedGroupIndexMap::const_iterator group = bandedGroups.find(batchKey);
            if (group != bandedGroups.end()) {
                // 配置はバンド管理へ預け、積み込みは視点に合わせて行われる。
                const rs::core::Mat4 world =
                    rs::world::ResolveWorldMatrix(entity.placement, ctx.editor.graph);
                ctx.sceneRenderer.BandedStaticInstances().AddMember(group->second, world.m);
                UpdateHeadlightRoster(ctx, entity);
                ++instancedCount;
                return;
            }
        }
        const std::unordered_map<std::string, bool>::const_iterator it = batchOk.find(batchKey);
        if (it == batchOk.end() || !it->second) return;
        AddStaticSceneryInstance(ctx, entity, batchKey);
        ++instancedCount;
    });
    return instancedCount;
}

bool ApplySwitchChangesToSceneryInstances(
    const EditorViewportContext& ctx,
    const std::vector<rs::world::SceneRegistry::SwitchChange>& changes) {
    if (changes.empty()) return true;
    static const bool kProfileSwitch = rs::platform::ReadEnvironmentVariable("RS_PROFILE_LOAD") != nullptr;
    const std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();

    // 影響を受けるバッチキー(旧キーと新キー)とそのフォルダ。
    std::unordered_set<std::string> affectedKeys;
    std::unordered_set<std::string> affectedFolders;
    std::vector<PendingInstanceBatch> pending; // 新キーのうちGPU側に未作成のもの
    for (const rs::world::SceneRegistry::SwitchChange& change : changes) {
        const rs::world::SceneEntity* entity = FindDrawnEntity(ctx, change.entityId);
        if (entity == nullptr || entity->category != rs::world::SceneEntityCategory::Scenery) return false;
        const TrackPluginEntry* pierEntry = nullptr;
        if (IsOrphanPier(ctx, *entity, pierEntry)) return false;
        // 新しい組が静的にできなくなっても古い灯具が残らないよう、一度外す(積み直しで入れ直す)。
        ctx.sceneRenderer.StaticHeadlights().Erase(entity->guid);
        const std::vector<int32_t>* optionSets[2] = {&change.previousSwitchOptions,
                                                     &entity->switchOptions};
        for (int side = 0; side < 2; ++side) {
            const StaticMeshVariantSet* variants = nullptr;
            // 静的化の判定を通らない組(スロット経路)はバッチを持たない。全再構築と同じく対象外。
            if (!ctx.sceneRenderer.TryGetStaticInstanceVariants(entity->folderUtf8, *optionSets[side],
                                                                 ctx.log, &variants, entity->isSignalModel, entity->definitionFile))
                continue;
            if (variants->NeedsVariantSwitching() || variants->variants.size() != 1) return false;
            const std::string key = rs::app::SceneRenderer::MakeStaticInstanceKey(
                entity->folderUtf8, *optionSets[side], entity->isSignalModel, entity->definitionFile);
            affectedKeys.insert(key);
            affectedFolders.insert(entity->folderUtf8);
            // 新キー側だけ、バッチが無ければ作る(旧キーのバッチは他の個体が使っていれば既にある)。
            // 全パーツ非表示で空のメッシュは、全再構築と同じくバッチを作らない。
            const rs::render::Mesh& mesh = variants->variants[0];
            if (side == 1 && !mesh.Empty() && !ctx.app.InstanceBatches().HasBatch(key)) {
                bool queued = false;
                for (const PendingInstanceBatch& p : pending) queued = queued || p.key == key;
                if (!queued) pending.push_back({key, &mesh});
            }
        }
    }

    // GPUアップロードは新規バッチ全部を1回のRunUploadCommandsにまとめる。
    if (!pending.empty()) {
        ctx.app.RunUploadCommands([&](ID3D12Device* device, DirectX::ResourceUploadBatch& upload) {
            std::vector<std::string> texturePaths;
            for (const PendingInstanceBatch& p : pending) {
                ctx.app.InstanceBatches().CollectNewBatchTexturePaths(p.key, *p.mesh, texturePaths);
            }
            rs::render::PrefetchImages(texturePaths);
            for (const PendingInstanceBatch& p : pending) {
                ctx.app.InstanceBatches().EnsureBatch(device, upload, p.key, *p.mesh);
            }
        });
    }

    // 影響キーのバッチだけ配置を空にして、同じキーの個体を積み直す。
    // 旧キーのバッチが空になっても解放しない(次の全再構築のPruneに任せる)。
    ctx.app.InstanceBatches().ClearInstances(affectedKeys);
    int readdedCount = 0;
    ForEachDrawnEntity(ctx, [&](const rs::world::SceneEntity& entity) {
        if (entity.category != rs::world::SceneEntityCategory::Scenery) return;
        if (affectedFolders.count(entity.folderUtf8) == 0) return;
        const std::string key = rs::app::SceneRenderer::MakeStaticInstanceKey(entity.folderUtf8, entity.switchOptions,
                                                                               entity.isSignalModel, entity.definitionFile);
        if (affectedKeys.count(key) == 0) return;
        AddStaticSceneryInstance(ctx, entity, key);
        ++readdedCount;
    });

    if (kProfileSwitch) {
        const double ms = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - t0).count();
        spdlog::info("[switchprof] instDiff={:.2f}ms changes={} keys={} newBatches={} readded={}", ms,
                     changes.size(), affectedKeys.size(), pending.size(), readdedCount);
    }
    return true;
}

bool ApplyPlacementChangesToSceneryInstances(
    const EditorViewportContext& ctx,
    const std::vector<rs::world::SceneRegistry::PlacementChange>& changes) {
    if (changes.empty()) return true;
    std::unordered_set<std::string> affectedKeys;
    std::unordered_set<std::string> affectedFolders;
    for (const auto& change : changes) {
        const rs::world::SceneEntity* entity = FindDrawnEntity(ctx, change.entityId);
        if (entity == nullptr || entity->category != rs::world::SceneEntityCategory::Scenery) return false;
        const TrackPluginEntry* pierEntry = nullptr;
        if (IsOrphanPier(ctx, *entity, pierEntry)) return false;
        const StaticMeshVariantSet* variants = nullptr;
        if (!ctx.sceneRenderer.TryGetStaticInstanceVariants(entity->folderUtf8, entity->switchOptions,
                                                             ctx.log, &variants, entity->isSignalModel, entity->definitionFile))
            continue;
        if (variants->NeedsVariantSwitching() || variants->variants.size() != 1) return false;
        affectedKeys.insert(rs::app::SceneRenderer::MakeStaticInstanceKey(
            entity->folderUtf8, entity->switchOptions, entity->isSignalModel, entity->definitionFile));
        affectedFolders.insert(entity->folderUtf8);
        ctx.sceneRenderer.StaticHeadlights().Erase(entity->guid);
    }
    ctx.app.InstanceBatches().ClearInstances(affectedKeys);
    ForEachDrawnEntity(ctx, [&](const rs::world::SceneEntity& entity) {
        if (entity.category != rs::world::SceneEntityCategory::Scenery ||
            affectedFolders.count(entity.folderUtf8) == 0) return;
        const std::string key = rs::app::SceneRenderer::MakeStaticInstanceKey(
            entity.folderUtf8, entity.switchOptions, entity.isSignalModel, entity.definitionFile);
        if (affectedKeys.count(key) != 0) AddStaticSceneryInstance(ctx, entity, key);
    });
    return true;
}

} // namespace rs::app

#include "SceneTrainBatching.h"
#include "ParsedPluginCache.h"

#include "../gpu/App.h"
#include "../plugin_session/PluginRuntimeApplier.h"
#include "../plugin_session/PreloadTextureSet.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <unordered_map>
#include <utility>

#include <spdlog/spdlog.h>

namespace rs::app {
namespace {
// 小編成ではスロット描画の方が有利な可能性があるため、2両以上でバッチにする。
constexpr size_t kMinimumInstancesPerBatch = 2;

// バッチ新規作成時、そのバッチが受け取りうる全ChangeTexture/SetAnimation候補を
// 事前登録しておく(founding車両のsessionから、CollectPreloadTextures経由でCAMDIST帯
// 等のシステムスイッチ仮定値を総当りした候補集合を得る)。
//
// バッチのMeshTexturePool(rs::render::TrainInstanceBatch::pipeline_)は編成内の
// 全車両で共有される。ここで事前登録せず、実行時にResolveBatchTextureOverrides
// (TryAddInstance内、車両ごと毎フレーム)経由でGetOrCreateTexGroupへ都度渡すと、
// 実行時にResolveBatchTextureOverridesで未登録の候補が来ると、同期GPUアップロードの
// フォールバックに落ちて一瞬固まる。到達前に全候補を登録しておき、それを避ける。
void PrewarmBatchDynamicTextures(ID3D12Device* device, DirectX::ResourceUploadBatch& upload,
                                 rs::render::TrainInstanceBatchManager& batches,
                                 const std::string& batchKey, const PluginSession& session) {
    const PreloadTextureSet preload = CollectPreloadTextures(session);
    rs::render::InstanceMatOverride discard;
    for (const std::array<std::string, 3>& paths : preload.materialTextures) {
        if (paths[0].empty()) continue;
        batches.PreloadTextureOverride(device, upload, batchKey, paths[0], paths[1], paths[2], discard);
    }
    for (const std::string& base : preload.animationTextures) {
        if (base.empty()) continue;
        batches.PreloadTextureOverride(device, upload, batchKey, base, "", "", discard);
    }
}

// PruneUnused に渡す猶予フレーム数。_CAMDIST量子化ヒステリシスによるバッチキー往復を
// 吸収するため、LOD境界での数フレーム分の揺れ動きより十分大きく取る。
// GPU資源の解放可否は、この猶予と別にフェンスで判定する。
constexpr UINT kBatchPruneGraceFrames = 120;

// バッチが画像を張った(使用フラグを画像の有無で立てた)あとに、EnvMap/BumpMap の指定が
// 成り立たない材質では法線/ORMの使用フラグを落とす。
void ApplyNormalOrmGate(const SceneMaterialOverrides::TextureOverride& textureOverride,
                        rs::render::InstanceMatOverride& value) {
    if (!textureOverride.allowNormal) value.flags[0] = 0.0f;
    if (!textureOverride.allowOrm) value.flags[1] = 0.0f;
}

bool ResolveBatchTextureOverrides(
    App& app, rs::render::TrainInstanceBatchManager& batches, const std::string& batchKey,
    SceneMaterialOverrides& overrides) {
    std::vector<size_t> missingTextureOverrides;
    for (const SceneMaterialOverrides::TextureOverride& textureOverride : overrides.textureOverrides) {
        if (textureOverride.valueIndex >= overrides.values.size()) return false;
        rs::render::InstanceMatOverride& value = overrides.values[textureOverride.valueIndex];
        if (!textureOverride.useTexture) {
            value.flags[0] = 0.0f;
            value.flags[1] = 0.0f;
            value.tex[1] = 0u;
            continue;
        }
        if (batches.ApplyPreloadedTextureOverride(batchKey, textureOverride.basePath,
                                                  textureOverride.normalPath,
                                                  textureOverride.pbrPath, value)) {
            ApplyNormalOrmGate(textureOverride, value);
            continue;
        }
        if (batches.TextureUnavailable(batchKey, textureOverride.basePath,
                                       textureOverride.normalPath, textureOverride.pbrPath)) return false;
        missingTextureOverrides.push_back(textureOverride.valueIndex);
    }
    if (missingTextureOverrides.empty()) return true;

    bool uploaded = true;
    // 描画フレームの途中なのでGPUの完了を待たない(同じキューに積むので、このフレームの描画より先に終わる)。
    app.RunUploadCommands(
        [&](ID3D12Device* device, DirectX::ResourceUploadBatch& upload) {
            for (const size_t valueIndex : missingTextureOverrides) {
                const SceneMaterialOverrides::TextureOverride& textureOverride =
                    *std::find_if(overrides.textureOverrides.begin(), overrides.textureOverrides.end(),
                                  [valueIndex](const SceneMaterialOverrides::TextureOverride& candidate) {
                                      return candidate.valueIndex == valueIndex;
                                  });
                if (!batches.PreloadTextureOverride(device, upload, batchKey, textureOverride.basePath,
                                                    textureOverride.normalPath, textureOverride.pbrPath,
                                                    overrides.values[valueIndex])) {
                    uploaded = false;
                    return;
                }
                ApplyNormalOrmGate(textureOverride, overrides.values[valueIndex]);
            }
        },
        UploadCompletion::NoWait);
    return uploaded;
}
}

void SceneTrainBatching::BeginFrame(App& app, const std::vector<SceneSlotPool::Slot>& slots) {
    app.TrainInstanceBatches().ClearInstances();
    currentFrameStats_ = {};
    batchInstanceCounts_.clear();
    slotBatchKeys_.assign(slots.size(), {});
    slotStaticOverrideCaches_.resize(slots.size());

    std::unordered_set<std::string> contentKeys;
    std::unordered_set<std::string> folders;
    const std::chrono::steady_clock::time_point indexStart = std::chrono::steady_clock::now();
    for (size_t slotIndex = 0; slotIndex < slots.size(); ++slotIndex) {
        const SceneSlotPool::Slot& slot = slots[slotIndex];
        if (!slot.used || !slot.isTrainCar || !slot.carSnapshot.carSnapshotValid) {
            slotStaticOverrideCaches_[slotIndex] = {};
            continue;
        }

        const std::string batchKey = MakeBatchKey(slot);
        if (batchKey.empty()) {
            slotStaticOverrideCaches_[slotIndex] = {};
            continue;
        }
        slotBatchKeys_[slotIndex] = batchKey;
        ++batchInstanceCounts_[batchKey];
        contentKeys.insert(slot.residency.contentKey);
        folders.insert(slot.residency.folderUtf8);
        ++currentFrameStats_.indexedSlots;
    }
    currentFrameStats_.indexTime = std::chrono::steady_clock::now() - indexStart;
    currentFrameStats_.distinctKeys = batchInstanceCounts_.size();
    currentFrameStats_.distinctContentKeys = contentKeys.size();
    currentFrameStats_.distinctFolders = folders.size();
    currentFrameStats_.keysWithCount1 = 0;
    currentFrameStats_.keysWithCount2 = 0;
    currentFrameStats_.keysWithCount3to4 = 0;
    currentFrameStats_.keysWithCount5plus = 0;
    for (const std::pair<const std::string, size_t>& entry : batchInstanceCounts_) {
        if (entry.second == 1) ++currentFrameStats_.keysWithCount1;
        else if (entry.second == 2) ++currentFrameStats_.keysWithCount2;
        else if (entry.second <= 4) ++currentFrameStats_.keysWithCount3to4;
        else ++currentFrameStats_.keysWithCount5plus;
    }
}

void SceneTrainBatching::RefreshSlotBatchKey(size_t slotIndex,
                                               const SceneSlotPool::Slot& slot) {
    const std::string batchKey = MakeBatchKey(slot);
    ++currentFrameStats_.batchKeyRefreshes;
    UpdateSlotBatchKey(slotIndex, batchKey);
}

bool SceneTrainBatching::TryAddInstance(App& app, size_t slotIndex, SceneSlotPool::Slot& slot,
                                          const std::vector<rs::render::PartXform>& partWorlds) {
    const std::chrono::steady_clock::time_point eligibilityStart =
        std::chrono::steady_clock::now();
    ++currentFrameStats_.eligibilityChecks;
    if (slot.carSnapshot.carFadeAlpha < 1.0f) return false;
    if (slotIndex >= slotBatchKeys_.size()) return false;
    const std::string& batchKey = slotBatchKeys_[slotIndex];
    if (batchKey.empty()) return false;
    for (const rs::legacy_pi::ModelPart& part : slot.session.plugin.def.parts)
        if (part.hasUnresolvedJointRef) return false;
    // 静的assemblyMeshを持たない車両(可動部のみで構成される等)はEnsureBatchが
    // 必ず失敗するため、毎フレームの無駄なGPUアップロード試行を避けて早期に
    // フォールバック経路へ回す(大量車両レイアウトで一部の
    // バッチキーがこれに該当すると、EnsureBatch再試行だけで1フレームあたり
    // 数十msを消費する)。
    if (slot.session.plugin.assemblyMesh.vertices.empty() ||
        slot.session.plugin.assemblyMesh.indices.empty()) return false;

    currentFrameStats_.eligibilityTime += std::chrono::steady_clock::now() - eligibilityStart;
    const std::unordered_map<std::string, size_t>::const_iterator countIt =
        batchInstanceCounts_.find(batchKey);
    if (countIt == batchInstanceCounts_.end() ||
        countIt->second < kMinimumInstancesPerBatch) return false;

    if (failedBatchKeys_.contains(batchKey)) return false;
    rs::render::TrainInstanceBatchManager& batches = app.TrainInstanceBatches();
    if (!batches.HasBatch(batchKey)) {
        const std::chrono::steady_clock::time_point ensureBatchStart = std::chrono::steady_clock::now();
        ++currentFrameStats_.ensureBatchCalls;
        bool uploaded = false;
        // 視点距離のモデル切替で走行中に作り直すので、GPUの完了を待たない。
        app.RunUploadCommands(
            [&](ID3D12Device* device, DirectX::ResourceUploadBatch& upload) {
                uploaded = batches.EnsureBatch(device, upload, batchKey, slot.session.plugin.assemblyMesh,
                                                static_cast<UINT>(partWorlds.size()),
                                                slot.session.DynamicMaterialIndices());
                if (uploaded) {
                    PrewarmBatchDynamicTextures(device, upload, batches, batchKey, slot.session);
                }
            },
            UploadCompletion::NoWait);
        currentFrameStats_.ensureBatchTime += std::chrono::steady_clock::now() - ensureBatchStart;
        if (!uploaded) {
            ++currentFrameStats_.ensureBatchFailures;
            // 内容(mesh/switch確定値等)が変わらない限り再試行しても結果は同じなので、
            // このキーを恒久失敗として記録し、以後はEnsureBatchを呼ばずに即falseへ回す。
            failedBatchKeys_.insert(batchKey);
            spdlog::warn("[train_batching] EnsureBatch失敗(以後このキーは再試行しません) key={}",
                        batchKey);
            return false;
        }
    }
    const std::vector<rs::render::MatData>* baseMaterialData = batches.MaterialData(batchKey);
    if (!baseMaterialData) return false;
    SlotStaticOverrideCache& cache = slotStaticOverrideCaches_[slotIndex];
    const bool force = cache.batchKey != batchKey;
    const bool changed = force || cache.switchGeneration != slot.session.SwitchGeneration();
    SessionRuntimeTimings timings;
    SceneMaterialOverrides overrides = ResolveSceneMaterialOverrides(
        slot.session, *baseMaterialData, cache.switchGeneration, cache.overrides, force, &timings);
    cache.batchKey = batchKey;
    currentFrameStats_.staticOverrideResolveTime += timings.materials;
    currentFrameStats_.animationOverlayTime += timings.animations;
    if (changed) ++currentFrameStats_.staticOverrideResolves;
    else ++currentFrameStats_.staticOverrideCacheHits;
    const std::chrono::steady_clock::time_point textureStart = std::chrono::steady_clock::now();
    const bool textureOk = ResolveBatchTextureOverrides(app, batches, batchKey, overrides);
    currentFrameStats_.textureOverrideTime += std::chrono::steady_clock::now() - textureStart;
    if (!textureOk) return false;
    const std::chrono::steady_clock::time_point addInstanceStart = std::chrono::steady_clock::now();
    // 車両の識別は「スロット番号とスロットの世代」。同じ番号に別の車両が入れば世代が進む。
    const uint64_t instanceKey = rs::render::CombineContinuityKey(
        static_cast<uint64_t>(slotIndex), slot.residency.generation);
    const bool added = batches.AddInstance(batchKey, partWorlds, overrides.values, instanceKey);
    currentFrameStats_.addInstanceTime += std::chrono::steady_clock::now() - addInstanceStart;
    if (!added) return false;
    app.SetSceneSlotVisible(static_cast<UINT>(slotIndex), false);
    return true;
}

void SceneTrainBatching::EndFrame(App& app) {
    std::unordered_set<std::string> liveKeys;
    liveKeys.reserve(batchInstanceCounts_.size());
    for (const std::pair<const std::string, size_t>& entry : batchInstanceCounts_)
        liveKeys.insert(entry.first);
    const size_t erasedCount = app.TrainInstanceBatches().PruneUnused(liveKeys, kBatchPruneGraceFrames);
    // Prune で実際にバッチが破棄された場合のみ負のキャッシュをクリアする。
    // failedBatchKeys_(TryAddInstance内、EnsureBatch失敗時に登録)はSRVプール枯渇による
    // 失敗も「内容不変なら再試行しても無駄」として恒久失敗扱いにしているため、Pruneで
    // プールに空きができても、負のキャッシュが残ったままだと二度と再試行されず、
    // Pruneの効果が意味を成さなくなる。破棄が起きた時だけクリアすることで、
    // 通常フレーム(何も破棄されない大多数のフレーム)では無駄な再試行の波を起こさない。
    if (erasedCount > 0) failedBatchKeys_.clear();
}

std::string SceneTrainBatching::MakeBatchKey(const SceneSlotPool::Slot& slot) {
    if (slot.session.HasDynamicStretchZy()) return {};
    // バッチキーにはslot.residency.contentKey(フォルダ+スイッチ確定値)を含める。一時期folderUtf8のみへ
    // 緩めたが、それだと同一フォルダ・同一ジオメトリでスイッチ確定値だけが異なる配置が
    // 1バッチへ統合され、先着インスタンスの焼き込み済みマテリアルが他インスタンスへ
    // 漏れる(スイッチでテクスチャ上書きが非activeになる個体は、ベース値として
    // バッチ共有のbaseMaterialDataを引くため、先着個体のテクスチャを被る)。
    // InstanceMatOverrideが吸収できるのは「上書きがactiveなマテリアル」だけで、
    // ベース値そのものの差は吸収できない。バッチ数の削減より表示の正しさを優先する。
    return MakePluginMeshKey(slot.residency.contentKey) + "|" + slot.session.ResolvedGeometrySignature();
}

void SceneTrainBatching::UpdateSlotBatchKey(size_t slotIndex, const std::string& batchKey) {
    if (slotIndex >= slotBatchKeys_.size()) return;

    std::string& previousKey = slotBatchKeys_[slotIndex];
    if (previousKey == batchKey) return;
    if (!previousKey.empty()) {
        std::unordered_map<std::string, size_t>::iterator previousCount =
            batchInstanceCounts_.find(previousKey);
        if (previousCount != batchInstanceCounts_.end()) {
            if (--previousCount->second == 0) batchInstanceCounts_.erase(previousCount);
        }
    }
    previousKey = batchKey;
    if (!batchKey.empty()) ++batchInstanceCounts_[batchKey];
}

} // namespace rs::app

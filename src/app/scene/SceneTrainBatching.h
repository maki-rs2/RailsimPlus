#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../../render/pipeline/MeshPipeline.h"
#include "../plugin_session/PluginRuntimeApplier.h"
#include "SceneSlotPool.h"

namespace rs::app {

class App;

// Train の描画だけをスロットからマルチパート・インスタンスバッチへ移す責務。
class SceneTrainBatching {
public:
    struct FrameStats {
        size_t eligibilityChecks = 0;
        size_t indexedSlots = 0;
        // 計測用: 全体を「車両全体のジオメトリ署名」単位でキー化した場合の
        // 分裂度合いを見るためのdistinctキー数。indexedSlotsに対してこれが小さいほど
        // 既にバッチ化がまとまっており、パーツ単位キー化の余地は小さいと判断できる。
        size_t distinctKeys = 0;
        // 上記distinctKeysのうち、同一キーのインスタンス数ごとの内訳(1台/2台/3-4台/5台以上)。
        // 「distinctKeysが多い理由」を切り分けるためのヒストグラム(単に断片化度合いだけを
        // 見るとキー1つあたりの規模を見誤るため)。
        size_t keysWithCount1 = 0;
        size_t keysWithCount2 = 0;
        size_t keysWithCount3to4 = 0;
        size_t keysWithCount5plus = 0;
        // ジオメトリ署名を含まないcontentKey(フォルダ+スイッチ確定値)単独でのdistinct数。
        // distinctKeysとの差が小さければ「そもそも別プラグイン(別車種)が多い」、
        // 差が大きければ「同一車種だがジオメトリ署名(ChangeModel解決結果)で分裂している」
        // と切り分けられる。
        size_t distinctContentKeys = 0;
        // contentKeyからさらにswitchOptionsも外した、folderUtf8(プラグインフォルダ)単独の
        // distinct数。distinctContentKeysとの差が大きければ、switchOptions(スイッチ確定値の
        // 生タプル)自体が分裂の主因(同一フォルダでも号車等の個体差スイッチが違うだけで
        // 別contentKeyになっている)と判断できる。
        size_t distinctFolders = 0;
        size_t batchKeyRefreshes = 0;
        size_t staticOverrideResolves = 0;
        size_t staticOverrideCacheHits = 0;
        std::chrono::nanoseconds indexTime{};
        std::chrono::nanoseconds eligibilityTime{};
        std::chrono::nanoseconds staticOverrideResolveTime{};
        std::chrono::nanoseconds animationOverlayTime{};
        // 計測用: TryAddInstance内でFrameStatsの他項目に含まれない残り区間
        // (EnsureBatch/テクスチャ解決/AddInstanceのCPU側コピー)の内訳。
        std::chrono::nanoseconds textureOverrideTime{};
        std::chrono::nanoseconds addInstanceTime{};
        std::chrono::nanoseconds ensureBatchTime{};
        size_t ensureBatchCalls = 0;
        size_t ensureBatchFailures = 0;
    };

    void BeginFrame(App& app, const std::vector<SceneSlotPool::Slot>& slots);
    // 1フレームの描画投入が終わったタイミングで呼ぶ(SubmitRenderSnapshotの末尾)。
    // このフレームで実際に使われたバッチキー集合(batchInstanceCounts_)を元に
    // TrainInstanceBatchManager::PruneUnused を呼び、使われなくなったバッチをGCする。
    void EndFrame(App& app);
    void RefreshSlotBatchKey(size_t slotIndex, const SceneSlotPool::Slot& slot);
    bool TryAddInstance(App& app, size_t slotIndex, SceneSlotPool::Slot& slot,
                        const std::vector<rs::render::PartXform>& partWorlds);
    const FrameStats& CurrentFrameStats() const { return currentFrameStats_; }

private:
    static std::string MakeBatchKey(const SceneSlotPool::Slot& slot);
    void UpdateSlotBatchKey(size_t slotIndex, const std::string& batchKey);

    struct SlotStaticOverrideCache {
        std::string batchKey;
        uint64_t switchGeneration = 0;
        SceneMaterialOverrides overrides;
    };

    std::unordered_map<std::string, size_t> batchInstanceCounts_;
    std::vector<std::string> slotBatchKeys_;
    std::vector<SlotStaticOverrideCache> slotStaticOverrideCaches_;
    // EnsureBatchが一度失敗したキーの負のキャッシュ。バッチ内容(mesh/switch確定値等)は
    // キーが変わらない限り不変なので、失敗は確定とみなし、EnsureBatchのGPU同期
    // アップロード試行を毎フレーム繰り返さない。
    std::unordered_set<std::string> failedBatchKeys_;
    FrameStats currentFrameStats_;
};

} // namespace rs::app

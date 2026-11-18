#include "../gpu/App.h"
//
// SceneSlotSynchronizerUpload.cpp
//
#include "SceneSlotSynchronizer.h"
#include "SceneSlotSyncInternal.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <unordered_map>
#include <utility>

#include "../plugin_session/PreloadTextureSet.h"
#include "../../render/pipeline/MeshPipeline.h"

#include <spdlog/spdlog.h>

namespace rs::app {

void SceneSlotSynchronizer::DeferBatchableCarUploads(SyncContext& ctx) {
    using ProfClock = std::chrono::steady_clock;
    std::vector<Slot>& slots = ctx.slots;
    std::vector<PendingUpload>& pendingUploads = ctx.pendingUploads;

    const ProfClock::time_point tDedup0 = ProfClock::now();
    std::unordered_map<std::string, size_t> initialBatchCandidateCounts;
    for (const PendingUpload& pendingUpload : pendingUploads) {
        const Slot& slot = slots[pendingUpload.slotIndex];
        if (!slot.isTrainCar) continue;
        bool hasUnresolvedJoint = false;
        for (const rs::legacy_pi::ModelPart& part : slot.session.plugin.def.parts) {
            if (!part.hasUnresolvedJointRef) continue;
            hasUnresolvedJoint = true;
            break;
        }
        if (hasUnresolvedJoint) continue;
        const std::string batchKey = MakePluginMeshKey(slot.residency.contentKey) + "|" +
                                     slot.session.ResolvedGeometrySignature();
        ++initialBatchCandidateCounts[batchKey];
    }

    const std::vector<PendingUpload>::iterator firstDeferredUpload = std::remove_if(
        pendingUploads.begin(), pendingUploads.end(),
                       [&initialBatchCandidateCounts, &slots](const PendingUpload& pendingUpload) {
            const Slot& slot = slots[pendingUpload.slotIndex];
            if (!slot.isTrainCar) return false;
            const std::string batchKey = MakePluginMeshKey(slot.residency.contentKey) + "|" +
                                         slot.session.ResolvedGeometrySignature();
            const std::unordered_map<std::string, size_t>::const_iterator countIt =
                initialBatchCandidateCounts.find(batchKey);
            return countIt != initialBatchCandidateCounts.end() && countIt->second >= 2;
        });
    requiresInitialCarBatchTick_ = firstDeferredUpload != pendingUploads.end();
    pendingUploads.erase(firstDeferredUpload, pendingUploads.end());
    if (ctx.profileEnabled)
        ctx.profile.dedupMs +=
            std::chrono::duration<double, std::milli>(ProfClock::now() - tDedup0).count();
}

PreloadTextureSet SceneSlotSynchronizer::CollectPreloadTexturesCached(const Slot& slot, size_t userSwitchBudget) {
    if (slot.isTrainCar || slot.residency.contentKey.empty()) return CollectPreloadTextures(slot.session, userSwitchBudget);
    const std::string key = slot.residency.contentKey + "|" + std::to_string(userSwitchBudget);
    const std::unordered_map<std::string, PreloadTextureSet>::const_iterator hit = preloadCache_.find(key);
    if (hit != preloadCache_.end()) return hit->second;
    return preloadCache_.emplace(key, CollectPreloadTextures(slot.session, userSwitchBudget)).first->second;
}

void SceneSlotSynchronizer::RunPendingUploads(SyncContext& ctx) {
    using ProfClock = std::chrono::steady_clock;
    std::vector<Slot>& slots = ctx.slots;
    std::vector<PendingUpload>& pendingUploads = ctx.pendingUploads;

    std::vector<bool> uploadOk(pendingUploads.size(), false);
    const ProfClock::time_point tUpload0 = ProfClock::now();
    const size_t submissionCount =
        (pendingUploads.size() + kSceneSlotUploadsPerSubmission - 1) /
        kSceneSlotUploadsPerSubmission;
    if (submissionCount > 1) {
        spdlog::debug("[scene] splitting {} slot uploads into {} submission(s)",
                     pendingUploads.size(), submissionCount);
    }
    ctx.app.RunUploadCommandsChunked(
        pendingUploads.size(), kSceneSlotUploadsPerSubmission,
        [&](ID3D12Device* device, DirectX::ResourceUploadBatch& upload, size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i) {
            const PendingUpload& pu = pendingUploads[i];
            uploadOk[i] = ctx.app.UploadSceneMeshWithBatch(
                device, upload, static_cast<UINT>(pu.slotIndex), pu.geometryKey,
                slots[pu.slotIndex].session.SceneUploadMesh());
        }

        // 時間駆動アニメ(SetAnimation)が再生中に参照するテクスチャを、このスロット
        // 専用のパイプラインへ事前ロードしておく(メッシュアップロードの後に行うこと。
        // MeshPipeline::UploadMeshがテクスチャスロットを初期化するため)。毎フレームの
        // 適用時に未ロードだとフォールバックの同期GPUアップロードが走り、GPU同期
        // ストームになる(PluginRuntimeApplier内の該当コメント参照)。normal/pbrは
        // 持たずbaseのみ。
        //
        // 対象は「スイッチ確定値の下で現在activeなエントリ」だけに絞る。プラグインの
        // DefineAnimation全フレームを先読みすると、多数の表示パターンを画像で持つ
        // 情報表示系プラグインでは千枚規模になり、スロット枠(kSceneEntityMaxTextures)・
        // VRAM・読込時間のいずれももたなくなるため。スイッチをGUIから
        // 変更した場合はcontentKey(folder+switchOptions)が変わるためrevive経路を
        // 通らず新規ロード(このループ)へ入り直す=active集合もその都度取り直される。
        //
        // ただし接近判定(_APPROACH1/2・_STOPPING)とカメラ距離LOD(_CAMDIST)は
        // contentKeyに含まれず配置後に値が変わるため、この根拠が成立しない。切り替わった
        // 瞬間に未ロードだと上記フォールバックへ落ちて一瞬固まる。
        // 対象範囲の決め方はCollectPreloadTextures(PreloadTextureSet.h)を参照。
        for (size_t i = begin; i < end; ++i) {
            if (!uploadOk[i]) continue;
            const size_t slotIndex = pendingUploads[i].slotIndex;
            // ユーザースイッチの軸も足す(配置後のスイッチ変更でテクスチャ読込が挟まらない
            // ように)。予算はスロット再利用でプールを保持する条件(kReuseTexSlotHeadroom)と
            // 同じ値にし、超えるプラグインは軸を足さない集合へ戻す。
            // 車両は対象外(予算0=軸を足さない集合)。編成のテクスチャは別経路(バッチ側)の
            // 事前登録と組み合わさるため、ここでユーザースイッチ軸まで広げない。
            const size_t userSwitchBudget =
                slots[slotIndex].isTrainCar ? 0 : kReuseTexSlotHeadroom;
            const PreloadTextureSet preload = CollectPreloadTexturesCached(slots[slotIndex], userSwitchBudget);
            if (preload.animationTextures.empty() && preload.materialTextures.empty()) continue;
            spdlog::debug("[scene] テクスチャ事前ロード: アニメ{}枚 + ChangeTexture{}枚 "
                         "(仮定値{}通り{}, ユーザースイッチ軸{}{}, 枠上限{}, {})",
                         preload.animationTextures.size(), preload.materialTextures.size(),
                         preload.hypothesisCount, preload.degraded ? "/縮退" : "",
                         preload.userSwitchAxisCount,
                         preload.userSwitchAxesUsed ? "(採用)"
                         : preload.userSwitchAxesOverBudget ? "(予算超過のため不採用)" : "",
                         kSceneEntityMaxTextures, slots[slotIndex].residency.folderUtf8);
            rs::render::MeshPipeline& pipeline = ctx.app.ScenePipeline(static_cast<UINT>(slotIndex));
            // 失敗(枠切れ/読込失敗)はMeshPipeline側が警告を出し、以後の毎フレーム再試行も
            // 止められる(MeshPipeline::TexBudgetExhausted)。スロット数ぶんログが膨らむのを
            // 避けるため、ここでは件ごとの警告は出さない。
            PreloadTextureEntries(pipeline, device, upload, preload, 0, PreloadEntryCount(preload),
                                  /*warnOnFailure=*/false);
        }
    });
    if (ctx.profileEnabled)
        ctx.profile.uploadMs +=
            std::chrono::duration<double, std::milli>(ProfClock::now() - tUpload0).count();
    // アップロード失敗(まれ: GPUバッファ作成失敗等)は、複数エンティティを1回の
    // バッチにまとめた都合上pop_backでのスロット詰め直しは行わず、単に未使用へ
    // 戻すだけにする(FindFreeSlot()が次回再利用する。slotsを縮めないだけで
    // 正しさには影響しない)。
    for (size_t i = 0; i < pendingUploads.size(); ++i) {
        if (uploadOk[i]) {
            slots[pendingUploads[i].slotIndex].hasGpuMesh = true;
            continue;
        }
        const PendingUpload& pu = pendingUploads[i];
        ctx.log.push_back("[scene] GPUアップロードに失敗しました: " + pu.folderUtf8);
        std::string contentKey = std::move(slots[pu.slotIndex].residency.contentKey);
        slots[pu.slotIndex] = Slot{};
        slotPool_.MarkEntityFailed(pu.entityId, std::move(contentKey));
    }
}

} // namespace rs::app

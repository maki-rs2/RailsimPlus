//
// SwitchFlipSmokeDriver.cpp
//
#include "SwitchFlipSmokeDriver.h"

#include <cstddef>
#include <unordered_map>

#include <spdlog/spdlog.h>

#include "../scene/SceneRenderer.h"
#include "../../plugins/schema/PluginSchema.h"
#include "../../world/alignment/GuidJson.h"
#include "../../world/scene/SceneRegistry.h"

namespace rs::app {

namespace {
constexpr int kSwitchFlipFrames[] = {60, 80, 100, 120, 140};
} // namespace

void SwitchFlipSmokeDriver::MaybeLogMeasuredSync(int frame, double syncMs) {
    if (switchFlipMeasureFrame_ >= 0 && frame == switchFlipMeasureFrame_) {
        spdlog::info("[switch-flip-smoke] frame={} スイッチ反映Sync所要={:.2f}ms", frame, syncMs);
        switchFlipMeasureFrame_ = -1;
    }
}

void SwitchFlipSmokeDriver::MaybeFlip(int frame, rs::world::SceneRegistry& sceneRegistry,
                                      SceneRenderer& sceneRenderer, std::vector<std::string>& log) {
    if (!active_ || switchFlipNextIndex_ >= static_cast<int>(std::size(kSwitchFlipFrames)) ||
        frame != kSwitchFlipFrames[switchFlipNextIndex_]) {
        return;
    }
    // 初回のみターゲット(スロット経路=TryGetStaticInstanceMeshがfalseを返す
    // Sceneryエンティティ、かつ選択肢2つ以上のスイッチを持つもの)を選定する。
    // 症状(同一フォルダが複数配置されそれぞれ異なるスイッチ値を持つ情報表示系
    // プラグイン)に寄せるため、同一folderUtf8が2件以上あるものを優先する
    // (1件しかないと単なるterrain設定用スイッチ等を拾ってしまうため)。
    if (rs::world::GuidIsNil(switchFlipEntityId_)) {
        std::unordered_map<std::string, int> folderCounts;
        for (const rs::world::SceneEntity& candidate : sceneRegistry.Entities()) {
            if (candidate.category != rs::world::SceneEntityCategory::Scenery) continue;
            ++folderCounts[candidate.folderUtf8];
        }

        auto tryPick = [&](bool requireDuplicateFolder) {
            for (const rs::world::SceneEntity& candidate : sceneRegistry.Entities()) {
                if (candidate.category != rs::world::SceneEntityCategory::Scenery) continue;
                if (requireDuplicateFolder && folderCounts[candidate.folderUtf8] < 2) continue;
                const rs::render::Mesh* staticMesh = nullptr;
                const bool isStaticInstanced = sceneRenderer.TryGetStaticInstanceMesh(
                    candidate.folderUtf8, candidate.switchOptions, log, &staticMesh);
                if (isStaticInstanced) continue; // 静的インスタンス化経路は対象外
                const std::vector<rs::legacy_pi::SwitchDef>* switchDefs =
                    sceneRenderer.TryGetSwitchDefs(candidate.folderUtf8, log);
                if (!switchDefs) continue;
                std::vector<size_t> multiValueIndices;
                for (size_t i = 0; i < switchDefs->size(); ++i) {
                    if ((*switchDefs)[i].entries.size() >= 2) multiValueIndices.push_back(i);
                }
                if (multiValueIndices.empty()) continue;
                switchFlipEntityId_ = candidate.guid;
                switchFlipFolderUtf8_ = candidate.folderUtf8;
                switchFlipSwitchIndices_ = multiValueIndices;
                spdlog::info("[switch-flip-smoke] frame={} target選定(同一folder{}件) "
                             "id={} folder={} switchable数={}",
                             frame, folderCounts[candidate.folderUtf8], rs::world::GuidToHex(switchFlipEntityId_),
                             switchFlipFolderUtf8_, switchFlipSwitchIndices_.size());
                return true;
            }
            return false;
        };
        if (!tryPick(/*requireDuplicateFolder=*/true)) tryPick(/*requireDuplicateFolder=*/false);

        if (rs::world::GuidIsNil(switchFlipEntityId_)) {
            // 対象データが無い環境(プラグイン未配置のレイアウト等)でも異常終了させず、
            // 警告だけ出してNフレーム完走→exit code 0で終える(以降の切替試行は
            // switchFlipEntityIdが無効値のままスキップされ続ける)。
            spdlog::warn("[switch-flip-smoke] frame={} スロット経路の対象エンティティが"
                         "見つかりません(切替をスキップします)",
                         frame);
        }
    }

    if (!rs::world::GuidIsNil(switchFlipEntityId_)) {
        const rs::world::SceneEntity* target = sceneRegistry.FindEntity(switchFlipEntityId_);
        const std::vector<rs::legacy_pi::SwitchDef>* switchDefs =
            sceneRenderer.TryGetSwitchDefs(switchFlipFolderUtf8_, log);
        if (target && switchDefs && !switchFlipSwitchIndices_.empty()) {
            const size_t switchIndex =
                switchFlipSwitchIndices_[static_cast<size_t>(switchFlipNextIndex_) %
                                         switchFlipSwitchIndices_.size()];
            const size_t entryCount = (*switchDefs)[switchIndex].entries.size();
            const int32_t currentValue =
                switchIndex < target->switchOptions.size() ? target->switchOptions[switchIndex] : 0;
            const int32_t newValue =
                static_cast<int32_t>((static_cast<size_t>(currentValue) + 1) % entryCount);
            spdlog::info("[switch-flip-smoke] frame={} id={} switch[{}]({}) {} -> {}", frame,
                         rs::world::GuidToHex(switchFlipEntityId_), switchIndex, (*switchDefs)[switchIndex].name,
                         currentValue, newValue);
            sceneRegistry.SetSwitchOption(switchFlipEntityId_, switchIndex, newValue);
            switchFlipMeasureFrame_ = frame + 1;
        }
    }
    ++switchFlipNextIndex_;
}

} // namespace rs::app

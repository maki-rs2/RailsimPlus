#include "PlacedSoundRequestCollector.h"

#include "../../platform/EnvironmentVariable.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <utility>

#include <spdlog/spdlog.h>

#include "../../platform/text/Encoding.h"
#include "../../plugins/schema/PluginSchema.h"
#include "../plugin_session/PluginSession.h"

namespace rs::app {
namespace {

// 音が鳴らない原因を「発動条件が偽」「ワールド位置が異常」「そもそも音源定義が無い」へ
// 切り分けるための診断出力。実機でしか再現しないため、環境変数で明示的に有効化する。
const bool kDumpPlacedSounds = rs::platform::ReadEnvironmentVariable("RS_DUMP_PLACED_SOUNDS") != nullptr;
constexpr int kDumpIntervalFrames = 120;

} // namespace

void AppendSessionSoundRequests(const PluginSession& session, std::size_t index, std::uint64_t generation,
                                bool isTrainCar, bool dumpThisFrame, const std::string& dumpLabel,
                                std::vector<rs::sim::PlacedSoundRequest>& out) {
    // If/ApplySwitch条件から外れたSoundEffectはrequestsへ含めない(=次のSync()で
    // 自動的に停止する。専用のpause APIは使わない設計、PlacedSoundSourcePool参照)。
    const rs::legacy_pi::SwitchResolver switchResolver =
        [&session](const std::string& name) -> int32_t { return session.ResolveSwitch(name); };

    const std::size_t count =
        std::min(session.soundEffectAttachments.size(), session.soundEffectStates.size());
    if (dumpThisFrame) {
        spdlog::info("[placed_sound] {} attachments={}", dumpLabel, count);
        // ドア等のスイッチはroutineSwitchOverrides経由で供給され、
        // ResolveSwitchがそちらを優先する。systemSwitchesだけ出しても
        // 実際に条件へ効いた値が分からないため両方を出す。
        for (const std::pair<const std::string, int32_t>& entry : session.SystemSwitches()) {
            spdlog::info("[placed_sound]   sysswitch {}={} (resolved={})", entry.first, entry.second,
                         session.ResolveSwitch(entry.first));
        }
        for (const std::pair<const std::string, int32_t>& entry : session.RoutineSwitchOverrides()) {
            spdlog::info("[placed_sound]   override {}={}", entry.first, entry.second);
        }
        for (const std::pair<const std::string, int32_t>& entry : session.UserSwitches()) {
            if (entry.second != 0) spdlog::info("[placed_sound]   userswitch {}={}", entry.first,
                                                entry.second);
        }
    }
    for (std::size_t attachmentIndex = 0; attachmentIndex < count; ++attachmentIndex) {
        const SoundEffectAttachment& attachment = session.soundEffectAttachments[attachmentIndex];
        const bool triggerActive = rs::legacy_pi::IsTriggerActive(attachment.trig, switchResolver);
        if (dumpThisFrame) {
            const SoundEffectRuntimeState& dumpState = session.soundEffectStates[attachmentIndex];
            spdlog::info(
                "[placed_sound]   [{}] wav={} resolved={} part={} loop={} trigger={} "
                "pos=({:.1f},{:.1f},{:.1f}) hasWorldPos={}",
                attachmentIndex, attachment.waveFile,
                attachment.resolvedWavPath.empty()
                    ? std::string("<unresolved>")
                    : rs::platform_text::WideToUtf8(attachment.resolvedWavPath),
                attachment.partDefIndex, attachment.loop ? 1 : 0, triggerActive ? 1 : 0,
                dumpState.worldPos.x, dumpState.worldPos.y, dumpState.worldPos.z,
                dumpState.hasWorldPos ? 1 : 0);
            // 条件が偽のときは、どの段で落ちたかを追えるよう発動条件チェーンを出す。
            if (!triggerActive) {
                for (const rs::legacy_pi::TriggerStep& step : attachment.trig.chain) {
                    spdlog::info("[placed_sound]     trig step cond='{}' negate={} switch='{}'",
                                 step.condition, step.negateCondition ? 1 : 0, step.switchName);
                }
            }
        }
        if (attachment.resolvedWavPath.empty()) continue;
        if (!triggerActive) continue;
        const SoundEffectRuntimeState& state = session.soundEffectStates[attachmentIndex];
        // 位置をまだ求めていない音源(Tickが走る前のスロット)は原点で鳴ってしまうので出さない。
        if (!state.hasWorldPos) continue;
        rs::sim::PlacedSoundRequest request;
        request.key = {index, attachmentIndex, generation};
        request.wavPath = attachment.resolvedWavPath;
        request.worldPos[0] = state.worldPos.x;
        request.worldPos[1] = state.worldPos.y;
        request.worldPos[2] = state.worldPos.z;
        request.velocity[0] = state.velocity.x;
        request.velocity[1] = state.velocity.y;
        request.velocity[2] = state.velocity.z;
        request.volumeLinear = std::pow(10.0f, static_cast<float>(attachment.volume) / 100.0f / 20.0f);
        request.loop = attachment.loop;
        request.trainSound = isTrainCar;
        out.push_back(std::move(request));
    }
}

std::vector<rs::sim::PlacedSoundRequest> CollectPlacedSoundRequests(
    const std::vector<SceneSlotPool::Slot>& slots, int& dumpFrameCounter) {
    std::vector<rs::sim::PlacedSoundRequest> requests;
    const bool dumpThisFrame = kDumpPlacedSounds && (dumpFrameCounter++ % kDumpIntervalFrames) == 0;
    for (std::size_t slotIndex = 0; slotIndex < slots.size(); ++slotIndex) {
        const SceneSlotPool::Slot& slot = slots[slotIndex];
        if (!slot.used) continue;
        // 編成車両はSubmitRenderSnapshotがワールド位置を書くまで原点のままなので、
        // スナップショット未確立のスロットは音源として出さない。
        if (slot.isTrainCar && !slot.carSnapshot.carSnapshotValid) continue;
        const std::string dumpLabel = dumpThisFrame
                                          ? "slot=" + std::to_string(slotIndex) +
                                                " train=" + (slot.isTrainCar ? "1" : "0") +
                                                " folder=" + slot.residency.folderUtf8
                                          : std::string();
        AppendSessionSoundRequests(slot.session, slotIndex, slot.residency.generation, slot.isTrainCar, dumpThisFrame, dumpLabel,
                                   requests);
    }
    return requests;
}

} // namespace rs::app

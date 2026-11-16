#include "ParticleRequestCollector.h"

#include <algorithm>
#include <cstddef>
#include <utility>

#include <spdlog/spdlog.h>

namespace rs::app {
namespace {

// キーの下位16ビットは添付の番号に使う。
constexpr std::size_t kAttachmentIndexBits = 16;
constexpr std::size_t kMaxAttachments = std::size_t{1} << kAttachmentIndexBits;

rs::core::DVec3 ToDouble(const rs::core::Vec3& value) {
    return {static_cast<double>(value.x), static_cast<double>(value.y), static_cast<double>(value.z)};
}

} // namespace

std::vector<rs::sim::ParticleEmitRequest> CollectParticleRequests(const std::vector<SceneSlotPool::Slot>& slots,
                                                                TextureSlotRegistry& textureSlots) {
    std::vector<rs::sim::ParticleEmitRequest> requests;
    for (std::size_t slotIndex = 0; slotIndex < slots.size(); ++slotIndex) {
        const SceneSlotPool::Slot& slot = slots[slotIndex];
        if (!slot.used) continue;
        // ワープ中と、その前後のフェード中は車両から放出しない。
        if (slot.isTrainCar && (!slot.carSnapshot.carSnapshotValid || !slot.carSnapshot.carVisible ||
                                slot.carSnapshot.carFadeAlpha < 1.0f || slot.carSnapshot.warpFadeInActive)) continue;
        const PluginSession& session = slot.session;
        if (session.particleAttachments.size() > kMaxAttachments) {
            spdlog::warn("[particle] slot={} has more than {} attachments; excess emitters skipped",
                         slotIndex, kMaxAttachments);
        }
        const std::size_t count = std::min({session.particleAttachments.size(), session.particleStates.size(),
                                             kMaxAttachments});
        const rs::legacy_pi::SwitchResolver switchResolver = [&session](const std::string& name) -> int32_t {
            return session.ResolveSwitch(name);
        };
        for (std::size_t attachmentIndex = 0; attachmentIndex < count; ++attachmentIndex) {
            const ParticleAttachment& attachment = session.particleAttachments[attachmentIndex];
            const ParticleRuntimeState& state = session.particleStates[attachmentIndex];
            if (attachment.partDefIndex < 0 || !state.hasWorld ||
                !rs::legacy_pi::IsTriggerActive(attachment.trig, switchResolver)) continue;
            rs::sim::ParticleEmitRequest request;
            request.key = {(slotIndex << kAttachmentIndexBits) | attachmentIndex, slot.residency.generation};
            request.params = attachment.params;
            request.params.textureSlot = textureSlots.Assign(attachment.texturePath);
            request.worldPosition = ToDouble(state.worldPos);
            for (std::size_t axis = 0; axis < state.worldAxes.size(); ++axis) {
                request.worldAxes[axis] = ToDouble(state.worldAxes[axis]);
            }
            requests.push_back(std::move(request));
        }
    }
    return requests;
}

} // namespace rs::app

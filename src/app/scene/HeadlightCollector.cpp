#include "HeadlightCollector.h"

#include <algorithm>

#include "../plugin_session/FlareTextureUpload.h"
#include "../plugin_session/LensFlareStyleConvert.h"
#include "../../world/scene/ScenePlacement.h"
#include "../plugin_session/PluginSession.h"

namespace rs::app {

void HeadlightCollector::AppendSession(const PluginSession& session, FlareTextureSlots& textureSlots,
                                       std::vector<rs::render::HeadlightLight>& out) {
    const rs::legacy_pi::SwitchResolver switchResolver = [&session](const std::string& name) -> int32_t {
        return session.ResolveSwitch(name);
    };
    const std::size_t count = std::min(session.headlightAttachments.size(), session.headlightStates.size());
    for (std::size_t i = 0; i < count; ++i) {
        const HeadlightAttachment& attachment = session.headlightAttachments[i];
        const HeadlightRuntimeState& state = session.headlightStates[i];
        // 位置をまだ求めていない灯具(Tickが走る前のスロット)は原点で光ってしまうので出さない。
        if (!state.hasWorld) continue;
        if (!rs::legacy_pi::IsTriggerActive(attachment.trig, switchResolver)) continue;

        out.push_back(MakeLight(attachment, state.worldPos, state.worldDir, textureSlots));
    }
}

rs::render::HeadlightLight HeadlightCollector::MakeLight(const HeadlightAttachment& attachment,
                                                         const rs::core::Vec3& position,
                                                         const rs::core::Vec3& direction,
                                                         FlareTextureSlots& textureSlots) {
    std::shared_ptr<const rs::render::FlareStyle>& style = styles_[attachment.styleKey];
    if (!style) {
        const TextureSlotAssigner assignSlot = [&textureSlots](const std::string& file) -> std::uint32_t {
            return file.empty() ? FlareTextureSlots::kUnassigned : textureSlots.Assign(file);
        };
        style = std::make_shared<const rs::render::FlareStyle>(ConvertLensFlare(attachment.flare, assignSlot));
    }
    rs::render::HeadlightLight light;
    light.position = position;
    light.direction = direction;
    light.maxDistance = attachment.maxDistance;
    light.style = style;
    return light;
}

void HeadlightCollector::AppendStatic(const StaticHeadlightSource& source, FlareTextureSlots& textureSlots,
                                      std::vector<rs::render::HeadlightLight>& out) {
    if (source.roster == nullptr || source.graph == nullptr || !source.findEntity) return;
    for (const StaticHeadlightRoster::EntryMap::value_type& item : source.roster->Entries()) {
        const rs::world::SceneEntity* entity = source.findEntity(item.first, item.second.signalModel);
        if (entity == nullptr) continue;
        const rs::core::Mat4 entityWorld = rs::world::ResolveWorldMatrix(entity->placement, *source.graph);
        const float position[3] = {entityWorld.m[12], entityWorld.m[13], entityWorld.m[14]};
        const std::vector<StaticHeadlight>* lights = item.second.Select(
            source.sharedState, source.hasView, source.eye, source.fovYRadians, position);
        if (lights == nullptr) continue;
        for (const StaticHeadlight& light : *lights) {
            const HeadlightRuntimeState pose =
                ComputeHeadlightWorldPose(light.attachment, light.partWorld, entityWorld);
            out.push_back(MakeLight(light.attachment, pose.worldPos, pose.worldDir, textureSlots));
        }
    }
}

std::vector<rs::render::HeadlightLight> HeadlightCollector::Collect(const std::vector<SceneSlotPool::Slot>& slots,
                                                                    const StaticHeadlightSource& staticSource,
                                                                    FlareTextureSlots& textureSlots,
                                                                    rs::render::LensFlarePass& pass) {
    std::vector<rs::render::HeadlightLight> lights;
    for (const SceneSlotPool::Slot& slot : slots) {
        if (!slot.used) continue;
        // 編成車両はスナップショットが確立するまで原点のままなので、出さない(音源と同じ)。
        if (slot.isTrainCar && !slot.carSnapshot.carSnapshotValid) continue;
        AppendSession(slot.session, textureSlots, lights);
    }
    AppendStatic(staticSource, textureSlots, lights);
    UploadPendingFlareTextures(textureSlots, pass);
    return lights;
}

}  // namespace rs::app

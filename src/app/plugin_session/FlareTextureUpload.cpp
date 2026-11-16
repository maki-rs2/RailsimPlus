#include "FlareTextureUpload.h"

#include <spdlog/spdlog.h>

#include "../../render/resource/TextureLoader.h"
#include "../../render/particle/ParticlePass.h"

namespace rs::app {

namespace {

template <class Pass>
void UploadPendingTextures(TextureSlotRegistry& slots, Pass& pass, const char* label) {
    if (!pass.Ready()) return;
    for (const TextureSlotRegistry::Pending& pending : slots.TakePending()) {
        const rs::render::ImageData image = rs::render::LoadImageRGBA(pending.path);
        if (!image.ok || !pass.UploadTexture(pending.slot, image)) {
            spdlog::warn("[{}] texture '{}' could not be read or uploaded; its elements are not drawn",
                         label, pending.path);
        }
    }
    if (slots.TakeOverflowNotice()) {
        spdlog::warn("[{}] more than {} distinct textures; the elements that need the rest are not drawn",
                     label, slots.Count());
    }
}

} // namespace

void UploadPendingFlareTextures(FlareTextureSlots& slots, rs::render::LensFlarePass& pass) {
    UploadPendingTextures(slots, pass, "lens_flare");
}

void UploadPendingParticleTextures(TextureSlotRegistry& slots, rs::render::ParticlePass& pass) {
    UploadPendingTextures(slots, pass, "particle");
}

}  // namespace rs::app

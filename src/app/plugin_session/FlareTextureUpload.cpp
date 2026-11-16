#include "FlareTextureUpload.h"

#include <spdlog/spdlog.h>

#include "../../render/resource/TextureLoader.h"

namespace rs::app {

void UploadPendingFlareTextures(FlareTextureSlots& slots, rs::render::LensFlarePass& pass) {
    if (!pass.Ready()) return;
    for (const FlareTextureSlots::Pending& pending : slots.TakePending()) {
        const rs::render::ImageData image = rs::render::LoadImageRGBA(pending.path);
        if (!image.ok || !pass.UploadTexture(pending.slot, image)) {
            spdlog::warn("[lens_flare] texture '{}' could not be read or uploaded; its elements are not drawn",
                         pending.path);
        }
    }
    if (slots.TakeOverflowNotice()) {
        spdlog::warn("[lens_flare] more than {} distinct textures; the elements that need the rest are not drawn",
                     rs::render::kMaxFlareTextures);
    }
}

}  // namespace rs::app

#include "SunFlareController.h"

#include <spdlog/spdlog.h>

#include "FlareTextureUpload.h"
#include "SunFlareSetup.h"

namespace rs::app {

void SunFlareController::Update(const ActiveEnv& activeEnv, FlareTextureSlots& slots,
                                rs::render::LensFlarePass& pass, rs::render::SunFlareSettings& settings) {
    if (activeEnv.Generation() == seenGeneration_) return;
    seenGeneration_ = activeEnv.Generation();

    // Texture 要素の画像は、Envのフォルダからの絶対パスにして台帳へ通す(灯具と同じ台帳)。
    const TextureSlotAssigner assignSlot = [&activeEnv, &slots](const std::string& file) -> std::uint32_t {
        const std::string path = activeEnv.ResolveFilePath(file);
        return path.empty() ? FlareTextureSlots::kUnassigned : slots.Assign(path);
    };
    SunFlareSetup setup = BuildSunFlareSetup(activeEnv.Definition(), assignSlot);
    UploadPendingFlareTextures(slots, pass);
    settings = std::move(setup.settings);
    spdlog::info("[lens_flare] env={} flare={} elements={} whiteout={}", activeEnv.Name(), settings.flare,
                 settings.style.elements.size(), settings.whiteout);
}

} // namespace rs::app

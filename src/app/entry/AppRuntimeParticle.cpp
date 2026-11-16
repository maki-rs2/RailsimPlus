#include "AppRuntimeFrameInternal.h"

#include <vector>

#include <spdlog/spdlog.h>

#include "../../platform/EnvironmentVariable.h"
#include "../scene/ParticleRequestCollector.h"

namespace rs::app {

void UpdateFrameParticles(FrameLoopState& loopState, bool loading) {
    AppRuntimeState& state = loopState.state;
    const std::vector<rs::sim::ParticleEmitRequest> requests =
        loading || !state.userSettings.miscParticle ? std::vector<rs::sim::ParticleEmitRequest>()
                                                    : CollectParticleRequests(state.sceneRenderer.Slots(),
                                                                                state.particleTextureSlots);
    state.particleSystem.Update(requests, static_cast<double>(loopState.io.DeltaTime),
                                 loopState.globals.gameClock.TimeScale(), {});
    static const bool kDumpParticles = rs::platform::ReadEnvironmentVariable("RS_DUMP_PARTICLES") != nullptr;
    constexpr int kDumpIntervalFrames = 120;
    if (kDumpParticles && loopState.frame % kDumpIntervalFrames == 0) {
        spdlog::info("[particle] emitters={} particles={}", state.particleSystem.EmitterCount(),
                     state.particleSystem.Pool().Size());
    }
}

} // namespace rs::app

#pragma once

#include <vector>

#include "../../sim/particle/ParticleSystem.h"
#include "SceneSlotPool.h"
#include "../plugin_session/TextureSlotRegistry.h"

namespace rs::app {

std::vector<rs::sim::ParticleEmitRequest> CollectParticleRequests(const std::vector<SceneSlotPool::Slot>& slots,
                                                               TextureSlotRegistry& textureSlots);

} // namespace rs::app

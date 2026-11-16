#pragma once

#include <cstdint>

#include "../../core/Math.h"

namespace rs::render {

std::uint32_t ShadowUpdateMask(std::uint64_t frameCounter, std::uint32_t cascadeCount,
                              const rs::core::Vec3& sunDirection,
                              const rs::core::Vec3& lastFullUpdateSunDirection,
                              float sunAngleThresholdDeg, bool historyValid, bool farSnapChanged);

} // namespace rs::render

#include "ShadowUpdateSchedule.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "CascadeFit.h"

namespace rs::render {

std::uint32_t ShadowUpdateMask(std::uint64_t frameCounter, std::uint32_t cascadeCount,
                              const rs::core::Vec3& sunDirection,
                              const rs::core::Vec3& lastFullUpdateSunDirection,
                              float sunAngleThresholdDeg, bool historyValid, bool farSnapChanged) {
    const std::uint32_t count =
        std::min(cascadeCount, static_cast<std::uint32_t>(kMaxShadowCascades));
    if (count == 0) return 0;
    const std::uint32_t allMask = (1u << count) - 1u;
    if (!historyValid || count <= 2 || farSnapChanged) return allMask;

    const double currentLength = std::sqrt(static_cast<double>(sunDirection.x) * sunDirection.x +
                                           static_cast<double>(sunDirection.y) * sunDirection.y +
                                           static_cast<double>(sunDirection.z) * sunDirection.z);
    const double previousLength = std::sqrt(
        static_cast<double>(lastFullUpdateSunDirection.x) * lastFullUpdateSunDirection.x +
        static_cast<double>(lastFullUpdateSunDirection.y) * lastFullUpdateSunDirection.y +
        static_cast<double>(lastFullUpdateSunDirection.z) * lastFullUpdateSunDirection.z);
    if (!(currentLength > 0.0) || !(previousLength > 0.0)) return allMask;
    const double cosine = std::clamp(
        (static_cast<double>(sunDirection.x) * lastFullUpdateSunDirection.x +
         static_cast<double>(sunDirection.y) * lastFullUpdateSunDirection.y +
         static_cast<double>(sunDirection.z) * lastFullUpdateSunDirection.z) /
            (currentLength * previousLength),
        -1.0, 1.0);
    const double angleDeg = std::acos(cosine) * 180.0 / std::numbers::pi;
    if (angleDeg >= std::max(0.0f, sunAngleThresholdDeg)) return allMask;

    std::uint32_t mask = 0x3u;
    mask |= 1u << ((frameCounter & 1u) == 0 ? 2u : 3u);
    return mask;
}

} // namespace rs::render

#pragma once

#include <cstdint>

#include "../../core/Math.h"

namespace rs::sim {

class ParticleNoise {
public:
    explicit ParticleNoise(std::uint64_t seed = 0) : seed_(seed) {}
    // 格子座標と格子の移動量を受け、各成分が[-1,1]の連続した値を返す。
    core::DVec3 Sample(const core::DVec3& position, double time) const;

private:
    double Scalar(const core::DVec3& position, std::uint64_t channel) const;
    std::uint64_t seed_;
};

} // namespace rs::sim

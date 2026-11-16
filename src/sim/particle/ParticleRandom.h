#pragma once

#include <cstdint>

namespace rs::sim {

class ParticleRandom {
public:
    explicit ParticleRandom(std::uint64_t seed = 0) : state_(seed) {}
    std::uint64_t Next();
    double Uniform(double first = 0.0, double second = 1.0);

private:
    std::uint64_t state_;
};

} // namespace rs::sim

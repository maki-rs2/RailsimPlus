#include "ParticleRandom.h"

#include <cmath>

namespace rs::sim {

std::uint64_t ParticleRandom::Next() {
    // SplitMix64の全ビット混合。状態0も有効なシードとして使える。
    state_ += 0x9e3779b97f4a7c15ULL;
    std::uint64_t value = state_;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

double ParticleRandom::Uniform(double first, double second) {
    // doubleの仮数に収まる53ビットを[0,1)へ写す。
    const double fraction = static_cast<double>(Next() >> 11U) * 0x1.0p-53;
    return std::lerp(first, second, fraction);
}

} // namespace rs::sim

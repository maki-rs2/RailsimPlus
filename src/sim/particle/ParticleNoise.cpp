#include "ParticleNoise.h"

#include "ParticleRandom.h"

#include <array>
#include <cmath>

namespace rs::sim {
namespace {

double Fade(double value) {
    // 端の1階・2階微分が0になる五次補間。
    return value * value * value * (value * (value * 6.0 - 15.0) + 10.0);
}

std::uint64_t LatticeIndex(double value) {
    // 周期を2のべき乗にし、負の座標も符号なし整数の範囲に戻す。
    constexpr double kPeriod = 65536.0;
    const double wrapped = std::fmod(value, kPeriod);
    return static_cast<std::uint64_t>(wrapped < 0.0 ? wrapped + kPeriod : wrapped);
}

} // namespace

double ParticleNoise::Scalar(const core::DVec3& position, std::uint64_t channel) const {
    const double baseX = std::floor(position.x);
    const double baseY = std::floor(position.y);
    const double baseZ = std::floor(position.z);
    const double fractionX = Fade(position.x - baseX);
    const double fractionY = Fade(position.y - baseY);
    const double fractionZ = Fade(position.z - baseZ);
    std::array<double, 8> corners{};
    for (std::size_t index = 0; index < corners.size(); ++index) {
        const std::uint64_t x = LatticeIndex(baseX + static_cast<double>(index & 1U));
        const std::uint64_t y = LatticeIndex(baseY + static_cast<double>((index >> 1U) & 1U));
        const std::uint64_t z = LatticeIndex(baseZ + static_cast<double>((index >> 2U) & 1U));
        ParticleRandom random(seed_ ^ (x | (y << 16U) | (z << 32U)) ^ channel);
        corners[index] = random.Uniform(-1.0, 1.0);
    }
    const double low = std::lerp(std::lerp(corners[0], corners[1], fractionX),
                                 std::lerp(corners[2], corners[3], fractionX), fractionY);
    const double high = std::lerp(std::lerp(corners[4], corners[5], fractionX),
                                  std::lerp(corners[6], corners[7], fractionX), fractionY);
    return std::lerp(low, high, fractionZ);
}

core::DVec3 ParticleNoise::Sample(const core::DVec3& position, double time) const {
    const core::DVec3 shifted = core::Add(position, {time, time, time});
    // 成分ごとに独立した系列から引き、方向の偏りを避ける。
    return {Scalar(shifted, 0x243f6a8885a308d3ULL), Scalar(shifted, 0x13198a2e03707344ULL),
            Scalar(shifted, 0xa4093822299f31d0ULL)};
}

} // namespace rs::sim

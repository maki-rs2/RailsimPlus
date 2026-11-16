#include "SignalSpeedColor.h"

#include <array>
#include <cmath>
#include <cstddef>

namespace rs::world {
namespace {

struct ColorStop {
    double speedKmh;
    SignalColor color;
};

constexpr std::array<ColorStop, 4> kStops = {{
    {0.0, {0.90f, 0.10f, 0.10f}},
    {45.0, {1.00f, 0.80f, 0.10f}},
    {120.0, {0.10f, 0.80f, 0.25f}},
    {160.0, {0.15f, 0.45f, 1.00f}},
}};

} // namespace

SignalColor SignalSpeedColor(double speedKmh) {
    if (speedKmh <= kStops.front().speedKmh) return kStops.front().color;
    if (speedKmh >= kStops.back().speedKmh) return kStops.back().color;
    for (size_t i = 1; i < kStops.size(); ++i) {
        const ColorStop& hi = kStops[i];
        if (speedKmh > hi.speedKmh) continue;
        const ColorStop& lo = kStops[i - 1];
        const double t = (speedKmh - lo.speedKmh) / (hi.speedKmh - lo.speedKmh);
        const float f = static_cast<float>(t);
        return SignalColor{std::lerp(lo.color.r, hi.color.r, f),
                           std::lerp(lo.color.g, hi.color.g, f),
                           std::lerp(lo.color.b, hi.color.b, f)};
    }
    return kStops.back().color;
}

} // namespace rs::world

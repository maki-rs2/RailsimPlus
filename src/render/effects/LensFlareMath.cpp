#include "LensFlareMath.h"

#include <algorithm>
#include <cmath>

namespace rs::render {

using rs::core::Vec3;

float ComputeFlareAlpha(float startCos, const Vec3& dir, const Vec3& toward, float vol) {
    const float angle = Vec3::Dot(dir.Normalized(), toward.Normalized() * -1.0f);
    if (angle <= startCos) return 0.0f;
    return vol * (angle - startCos) / (1.0f - startCos);
}

float ApplyTwinkle(float alpha, float twinkle, float random01) {
    const float low = 1.0f - twinkle;
    return alpha * (std::lerp(low, 1.0f, random01));
}

float HashRandom01(std::uint32_t frameIndex, std::uint32_t seed) {
    std::uint32_t h = frameIndex * 747796405u + seed * 2891336453u + 1u;
    h = ((h >> ((h >> 28u) + 4u)) ^ h) * 277803737u;
    h = (h >> 22u) ^ h;
    return static_cast<float>(h >> 8u) / 16777216.0f;
}

std::vector<FlareInstance> LayoutFlareElements(const FlareStyle& style, float alpha, const Vec3& pos, const Vec3& dir,
                                                const Vec3& cameraPos, const Vec3& cameraForward, float maxDistance) {
    std::vector<FlareInstance> out;
    if (style.elements.empty() || alpha <= 0.0f) return out;
    out.reserve(style.elements.size());
    const float range = (cameraPos - pos).Length();
    // approach は正規化しない(RailSim II と同じ)。
    const Vec3 approach = dir.Normalized() + cameraForward.Normalized() * style.inclination;
    Vec3 tpos = pos;
    float shift = 0.0f;
    for (const FlareElementSpec& element : style.elements) {
        // 減衰は tpos を進める前の位置で、alpha から要素ごとに求め直す(累積しない)。
        const float atn =
            alpha * (maxDistance < 0.0f ? 1.0f : (maxDistance - (tpos - pos).Length()) / maxDistance);
        tpos += approach * (range * (element.distance - shift));
        if (atn <= 0.0f) break;
        out.push_back({tpos, element.radius, atn, &element});
        shift = element.distance;
    }
    return out;
}

Vec3 SunFlarePosition(const Vec3& cameraPos, const Vec3& directionToSun) {
    return cameraPos + directionToSun.Normalized() * kSunFlareDistanceM;
}

float ComputeWhiteoutAlpha(float startCos, const Vec3& cameraForward, const Vec3& directionToSun, float vol) {
    const float angle = Vec3::Dot(directionToSun.Normalized(), cameraForward.Normalized());
    if (angle <= startCos) return 0.0f;
    return vol * (angle - startCos) / (1.0f - startCos);
}

} // namespace rs::render

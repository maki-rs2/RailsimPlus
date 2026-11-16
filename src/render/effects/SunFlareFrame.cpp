#include "SunFlareFrame.h"

namespace rs::render {

namespace {

// 太陽のまたたきの乱数のシード(灯具とずらすための番号)。
constexpr std::uint32_t kSunTwinkleSeed = 1;
// 太陽の光源の番号(frame.lights の先頭)。
constexpr std::uint32_t kSunLightIndex = 0;

} // namespace

void BuildSunFlareFrame(const SunFlareSettings& settings, const SunFlareFrameInput& input, LensFlareFrame& frame) {
    frame.lights.clear();
    frame.draws.clear();
    frame.whiteout = WhiteoutDraw{};
    if (input.vol <= 0.0f || input.directionToSun.Length() <= 0.0f) return;

    const rs::core::Vec3 toSun = input.directionToSun.Normalized();
    const rs::core::Vec3 forward = input.cameraForward.Normalized();
    const rs::core::Vec3 sunPosition = SunFlarePosition(input.cameraPos, toSun);

    const bool drawFlare = settings.flare && input.flareEnabled && !settings.style.elements.empty();
    if (drawFlare) {
        // 太陽の dir は太陽から見たカメラ向き(= -太陽の向き)、toward はカメラの前方(設計書の式の符号)。
        const rs::core::Vec3 dir = toSun * -1.0f;
        float alpha = ComputeFlareAlpha(settings.style.startCos, dir, forward, input.vol);
        if (alpha > 0.0f) {
            alpha = ApplyTwinkle(alpha, settings.style.twinkle, HashRandom01(input.frameIndex, kSunTwinkleSeed));
            const std::vector<FlareInstance> instances =
                LayoutFlareElements(settings.style, alpha, sunPosition, dir, input.cameraPos, forward, -1.0f);
            for (const FlareInstance& instance : instances) frame.draws.push_back({instance, kSunLightIndex});
        }
    }
    if (settings.whiteout && input.whiteoutEnabled) {
        const float strength = ComputeWhiteoutAlpha(settings.whiteoutStartCos, forward, toSun, input.vol);
        if (strength > 0.0f) {
            frame.whiteout.enabled = true;
            frame.whiteout.lightIndex = kSunLightIndex;
            frame.whiteout.color = settings.whiteoutColor;
            frame.whiteout.color.a *= strength;
        }
    }
    if (frame.HasWork()) frame.lights.push_back({sunPosition, true});
}

} // namespace rs::render

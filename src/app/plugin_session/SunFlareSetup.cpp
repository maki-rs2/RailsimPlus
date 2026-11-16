#include "SunFlareSetup.h"

#include <cstdint>

namespace rs::app {

namespace {

using rs::legacy_pi::LensFlareDef;
using rs::legacy_pi::LensFlareElement;
using rs::legacy_pi::PiColor;

// 組み込みの既定値の1行。色は #AARRGGBB。
struct BuiltInElement {
    LensFlareElement::Kind kind;
    double distance;
    double radius;
    std::uint32_t innerArgb;
    std::uint32_t outerArgb;
};

// 標準の空(Envなし)のときの既定値。RailSim II 標準の環境プラグインの Sun.LensFlare・Sun.Whiteout の値で、
// RailSim II はEnvが無いときこの定義を読む。Texture 要素は含まれない。
constexpr double kBuiltInFlareStartAngleDeg = 25.0;
constexpr double kBuiltInFlareTwinkle = 0.5;
constexpr double kBuiltInFlareInclination = 0.25;
constexpr BuiltInElement kBuiltInElements[] = {
    {LensFlareElement::Kind::Circle, 0.2, 2.0, 0x00000000, 0xe0804000},
    {LensFlareElement::Kind::Hexagon, 0.4, 2.5, 0x00000000, 0xe0806000},
    {LensFlareElement::Kind::Circle, 0.6, 1.5, 0x00000000, 0xe0001060},
    {LensFlareElement::Kind::Circle, 0.8, 3.0, 0x00000000, 0xe0006020},
    {LensFlareElement::Kind::Circle, 1.0, 1.0, 0xe0008040, 0x00000000},
    {LensFlareElement::Kind::Circle, 1.2, 4.0, 0x00000000, 0xe0002000},
};
constexpr double kBuiltInWhiteoutStartAngleDeg = 25.0;
constexpr std::uint32_t kBuiltInWhiteoutArgb = 0x80ffffff;

constexpr float kByteMax = 255.0f;

PiColor FromArgb(std::uint32_t argb) {
    PiColor color;
    color.a = static_cast<float>((argb >> 24) & 0xff) / kByteMax;
    color.r = static_cast<float>((argb >> 16) & 0xff) / kByteMax;
    color.g = static_cast<float>((argb >> 8) & 0xff) / kByteMax;
    color.b = static_cast<float>(argb & 0xff) / kByteMax;
    return color;
}

LensFlareDef BuiltInLensFlare() {
    LensFlareDef def;
    def.startAngle = kBuiltInFlareStartAngleDeg;
    def.twinkle = kBuiltInFlareTwinkle;
    def.inclination = kBuiltInFlareInclination;
    for (const BuiltInElement& row : kBuiltInElements) {
        LensFlareElement element;
        element.kind = row.kind;
        element.distance = row.distance;
        element.radius = row.radius;
        element.innerColor = FromArgb(row.innerArgb);
        element.outerColor = FromArgb(row.outerArgb);
        def.elements.push_back(element);
    }
    return def;
}

} // namespace

SunFlareSetup BuildSunFlareSetup(const rs::legacy_pi::EnvDefinition* env, const TextureSlotAssigner& assignSlot) {
    SunFlareSetup setup;
    rs::render::SunFlareSettings& settings = setup.settings;
    if (env == nullptr) {
        settings.style = ConvertLensFlare(BuiltInLensFlare(), assignSlot);
        settings.flare = true;
        settings.whiteout = true;
        settings.whiteoutStartCos = CosOfDegrees(kBuiltInWhiteoutStartAngleDeg);
        settings.whiteoutColor = ToFlareColor(FromArgb(kBuiltInWhiteoutArgb));
        return setup;
    }
    if (env->lensFlare.has_value()) {
        settings.style = ConvertLensFlare(*env->lensFlare, assignSlot);
        settings.flare = true;
    }
    if (env->whiteout.present) {
        settings.whiteout = true;
        settings.whiteoutStartCos = CosOfDegrees(env->whiteout.startAngle);
        settings.whiteoutColor = ToFlareColor(env->whiteout.color);
    }
    return setup;
}

} // namespace rs::app

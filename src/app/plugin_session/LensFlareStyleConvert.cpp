#include "LensFlareStyleConvert.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace rs::app {

namespace {

using rs::legacy_pi::LensFlareElement;

rs::render::FlareElementKind ToFlareKind(LensFlareElement::Kind kind) {
    switch (kind) {
        case LensFlareElement::Kind::Hexagon: return rs::render::FlareElementKind::Hexagon;
        case LensFlareElement::Kind::Texture: return rs::render::FlareElementKind::Texture;
        case LensFlareElement::Kind::Circle: break;
    }
    return rs::render::FlareElementKind::Circle;
}

}  // namespace

float CosOfDegrees(double degrees) {
    return static_cast<float>(std::cos(degrees * std::numbers::pi / 180.0));
}

rs::render::FlareColor ToFlareColor(const rs::legacy_pi::PiColor& color) {
    return {color.r, color.g, color.b, color.a};
}

rs::render::FlareStyle ConvertLensFlare(const rs::legacy_pi::LensFlareDef& def, const TextureSlotAssigner& assignSlot) {
    rs::render::FlareStyle style;
    style.startCos = CosOfDegrees(def.startAngle);
    style.twinkle = static_cast<float>(def.twinkle);
    style.inclination = static_cast<float>(def.inclination);
    // Distance の昇順(同じ値は宣言順)。
    std::vector<LensFlareElement> sorted = def.elements;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const LensFlareElement& a, const LensFlareElement& b) { return a.distance < b.distance; });
    for (const LensFlareElement& element : sorted) {
        rs::render::FlareElementSpec spec;
        spec.kind = ToFlareKind(element.kind);
        spec.distance = static_cast<float>(element.distance);
        spec.radius = static_cast<float>(element.radius);
        spec.innerColor = ToFlareColor(element.innerColor);
        spec.outerColor = ToFlareColor(element.outerColor);
        if (element.kind == LensFlareElement::Kind::Texture) spec.textureIndex = assignSlot(element.texFileName);
        style.elements.push_back(spec);
    }
    return style;
}

}  // namespace rs::app

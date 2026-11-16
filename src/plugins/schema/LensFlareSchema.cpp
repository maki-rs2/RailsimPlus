#include "LensFlareSchema.h"

#include "SchemaValueRead.h"
// 値の取り出し(GetString/GetDouble)は.rs2読込と同じヘルパーを共用する(header-onlyで依存はヘッダだけ)。
#include "../layout/PiValueAccessors.h"

namespace rs::legacy_pi {

namespace {

using rs::legacy_layout::GetDouble;
using rs::legacy_layout::GetString;

LensFlareElement ReadElement(const PiBlock& block, LensFlareElement::Kind kind, Diagnostics& diag) {
    LensFlareElement element;
    element.kind = kind;
    element.distance = GetDouble(block, "Distance", 0.0);
    element.radius = GetDouble(block, "Radius", 0.0);
    if (kind == LensFlareElement::Kind::Texture) {
        element.texFileName = GetString(block, "TexFileName");
        // Color の既定は不透明な白。外側の色も同じ値にする。
        PiColor color{1.0f, 1.0f, 1.0f, 1.0f};
        ReadColor(block, "Color", diag, color);
        element.innerColor = color;
        element.outerColor = color;
    } else {
        ReadColor(block, "InnerColor", diag, element.innerColor);
        ReadColor(block, "OuterColor", diag, element.outerColor);
    }
    return element;
}

} // namespace

LensFlareDef ReadLensFlare(const PiBlock& block, Diagnostics& diag) {
    LensFlareDef flare;
    flare.startAngle = GetDouble(block, "StartAngle", 0.0);
    flare.twinkle = GetDouble(block, "Twinkle", 0.0);
    flare.inclination = GetDouble(block, "Inclination", 0.0);
    // blocks は出現順なので、種類が混ざっていても宣言順のまま並ぶ。
    for (const PiBlock& child : block.blocks) {
        if (child.name == "Circle") {
            flare.elements.push_back(ReadElement(child, LensFlareElement::Kind::Circle, diag));
        } else if (child.name == "Hexagon") {
            flare.elements.push_back(ReadElement(child, LensFlareElement::Kind::Hexagon, diag));
        } else if (child.name == "Texture") {
            flare.elements.push_back(ReadElement(child, LensFlareElement::Kind::Texture, diag));
        }
    }
    return flare;
}

} // namespace rs::legacy_pi

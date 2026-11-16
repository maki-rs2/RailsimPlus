#include "OverlayColorTuning.h"

#include "../../core/ConfigJson.h"

#include <cctype>
#include <cstdlib>
#include <string>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "EmbeddedOverlayColors.h"

namespace rs::app {

namespace {

constexpr std::size_t kHexRgbLength = 7;  // "#rrggbb"
constexpr std::size_t kHexRgbaLength = 9; // "#rrggbbaa"
constexpr int kHexBase = 16;

struct ColorField {
    const char* key;
    uint32_t OverlayColorTuning::*member;
};

// 色のキーとメンバの対応。キーを足すときはここと構造体とJSONの3か所をそろえる。
constexpr ColorField kColorFields[] = {
    {"hover", &OverlayColorTuning::hover},
    {"select", &OverlayColorTuning::select},
    {"drag", &OverlayColorTuning::drag},
    {"invalid", &OverlayColorTuning::invalid},
    {"valid", &OverlayColorTuning::valid},
    {"pointIdle", &OverlayColorTuning::pointIdle},
    {"invalidLine", &OverlayColorTuning::invalidLine},
    {"invalidLineFaint", &OverlayColorTuning::invalidLineFaint},
    {"layingLine", &OverlayColorTuning::layingLine},
    {"layingTransition", &OverlayColorTuning::layingTransition},
    {"layingTransitionFill", &OverlayColorTuning::layingTransitionFill},
    {"layingOtherFill", &OverlayColorTuning::layingOtherFill},
    {"layingEndNode", &OverlayColorTuning::layingEndNode},
    {"layingAnchor", &OverlayColorTuning::layingAnchor},
    {"branchMarker", &OverlayColorTuning::branchMarker},
    {"splitMarker", &OverlayColorTuning::splitMarker},
    {"parallelSnapCursor", &OverlayColorTuning::parallelSnapCursor},
    {"edgeIdle", &OverlayColorTuning::edgeIdle},
    {"edgeSelected", &OverlayColorTuning::edgeSelected},
    {"edgeHovered", &OverlayColorTuning::edgeHovered},
    {"edgeMarkerIdle", &OverlayColorTuning::edgeMarkerIdle},
    {"edgeRoleStart", &OverlayColorTuning::edgeRoleStart},
    {"edgeRoleEnd", &OverlayColorTuning::edgeRoleEnd},
    {"sectionLine", &OverlayColorTuning::sectionLine},
    {"sectionRing", &OverlayColorTuning::sectionRing},
    {"signalExtent", &OverlayColorTuning::signalExtent},
    {"labelText", &OverlayColorTuning::labelText},
    {"labelStem", &OverlayColorTuning::labelStem},
    {"pluginTargetBase", &OverlayColorTuning::pluginTargetBase},
};

// "#rrggbb"/"#rrggbbaa"を内部の並び(下位バイトからR,G,B,A)へ。形式が違えばfalse。
bool ParseHexColor(const std::string& text, uint32_t* out) {
    if ((text.size() != kHexRgbLength && text.size() != kHexRgbaLength) || text.front() != '#') return false;
    for (std::size_t i = 1; i < text.size(); ++i) {
        if (!std::isxdigit(static_cast<unsigned char>(text[i]))) return false;
    }
    const auto byteAt = [&text](std::size_t index) {
        return static_cast<uint8_t>(std::strtoul(text.substr(index, 2).c_str(), nullptr, kHexBase));
    };
    const uint8_t alpha = text.size() == kHexRgbaLength ? byteAt(7) : 255;
    *out = rs::core::PackRgba(byteAt(1), byteAt(3), byteAt(5), alpha);
    return true;
}

OverlayColorTuning ParseEmbedded() {
    OverlayColorTuning tuning = rs::core::ReadEmbeddedConfig<OverlayColorTuning>(
        rs::embedded::kOverlayColorsJson, rs::embedded::kOverlayColorsJsonSize, "OverlayColors",
        [](const nlohmann::json& root) {
            OverlayColorTuning tuning;
            for (const ColorField& field : kColorFields) {
                if (!root.contains(field.key)) continue;
                uint32_t color = 0;
                if (root[field.key].is_string() && ParseHexColor(root[field.key].get<std::string>(), &color)) {
                    tuning.*field.member = color;
                } else {
                    spdlog::warn("OverlayColorTuning: キー{}が\"#rrggbbaa\"形式でないので既定値を使います", field.key);
                }
            }
            tuning.tintAlpha = root.value("tintAlpha", tuning.tintAlpha);
            tuning.blockSaturation = root.value("blockSaturation", tuning.blockSaturation);
            tuning.blockValue = root.value("blockValue", tuning.blockValue);
            return tuning;
        });
    return tuning;
}

} // namespace

const OverlayColorTuning& OverlayColorTuning::Embedded() {
    static const OverlayColorTuning kTuning = ParseEmbedded();
    return kTuning;
}

OverlayTint MakeOverlayTint(uint32_t packedColor, float alpha) {
    constexpr float kByteMax = 255.0f;
    return OverlayTint{{static_cast<float>(packedColor & 0xFFu) / kByteMax,
                        static_cast<float>((packedColor >> 8u) & 0xFFu) / kByteMax,
                        static_cast<float>((packedColor >> 16u) & 0xFFu) / kByteMax, alpha}};
}

} // namespace rs::app

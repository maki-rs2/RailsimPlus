//
// TrackLayingPreviewColor.cpp - 敷設プレビュー線の色
//
#include "TrackLayingPreviewColor.h"

#include <cstdio>

#include "../ui/OverlayColorTuning.h"

namespace rs::app {

uint32_t PreviewColor(rs::world::AlignmentSegment::Kind kind) {
    const OverlayColorTuning& colors = OverlayColorTuning::Embedded();
    if (kind == rs::world::AlignmentSegment::Kind::Line) return colors.layingLine;
    if (kind == rs::world::AlignmentSegment::Kind::Transition) return colors.layingTransition;
    return colors.valid;
}

std::string ToHexRgba(uint32_t packedColor) {
    const unsigned int red = packedColor & 0xFFu;
    const unsigned int green = (packedColor >> 8u) & 0xFFu;
    const unsigned int blue = (packedColor >> 16u) & 0xFFu;
    const unsigned int alpha = (packedColor >> 24u) & 0xFFu;
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X%02X", red, green, blue, alpha);
    return std::string(buffer);
}

} // namespace rs::app

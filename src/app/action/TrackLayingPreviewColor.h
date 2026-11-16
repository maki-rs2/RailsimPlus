//
// TrackLayingPreviewColor.h - 敷設プレビュー線の色(3Dのプレビュー線・ImGui版の凡例・RmlUi版の凡例で共用)
//
// 色の出どころはここだけ。3Dのプレビュー線(app/ui/ViewportOverlayGeometry.cpp)も、
// 凡例(ImGui版・RmlUi版)も、PreviewColor()から色を作る。
//
#pragma once

#include <cstdint>
#include <string>

#include "../../world/alignment/TrackGraph.h"

namespace rs::app {

// 区間の種類ごとのプレビュー線の色。IM_COL32と同じ並び(下位バイトからR,G,B,A)。
uint32_t PreviewColor(rs::world::AlignmentSegment::Kind kind);

// packedColor(PreviewColorと同じ並び)を"#RRGGBBAA"の文字列にする(RmlUiのcolor用)。
std::string ToHexRgba(uint32_t packedColor);

} // namespace rs::app

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "Math.h"

namespace rs::core {

constexpr uint32_t PackRgba(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha) {
    return static_cast<uint32_t>(red) | (static_cast<uint32_t>(green) << 8u) |
           (static_cast<uint32_t>(blue) << 16u) | (static_cast<uint32_t>(alpha) << 24u);
}

enum class DebugDashStyle { Solid, Dashed, Dotted };
enum class DebugDepthMode { Test, AlwaysOnTop };

struct DebugStyle {
    // IM_COL32 と同じ並び（下位バイトから R, G, B, A）。
    uint32_t color = 0xFFFFFFFFu;
    float widthPx = 1.0f;
    DebugDashStyle dash = DebugDashStyle::Solid;
    float dashPeriodPx = 12.0f;
    bool distanceAttenuation = true;
    DebugDepthMode depthMode = DebugDepthMode::Test;
};

struct DebugPolyline { std::vector<Vec3> points; bool closed = false; DebugStyle style; };
struct DebugCircle { Vec3 center; float radiusPx = 1.0f; DebugStyle style; };
struct DebugRect { Vec3 center; float halfWidthPx = 1.0f; float halfHeightPx = 1.0f; DebugStyle style; };
struct DebugText { Vec3 anchor; Vec2 offsetPx; std::string utf8; DebugStyle style; };

class DebugDrawList {
public:
    void Clear();
    void AddLine(const Vec3& a, const Vec3& b, const DebugStyle& style);
    void AddPolyline(const Vec3* points, size_t count, bool closed, const DebugStyle& style);
    void AddCircle(const Vec3& center, float radiusPx, const DebugStyle& style);
    void AddRect(const Vec3& center, float halfWidthPx, float halfHeightPx, const DebugStyle& style);
    void AddText(const Vec3& anchor, const Vec2& offsetPx, std::string_view utf8, const DebugStyle& style);
    const std::vector<DebugPolyline>& Polylines() const { return polylines_; }
    const std::vector<DebugCircle>& Circles() const { return circles_; }
    const std::vector<DebugRect>& Rects() const { return rects_; }
    const std::vector<DebugText>& Texts() const { return texts_; }
private:
    std::vector<DebugPolyline> polylines_;
    std::vector<DebugCircle> circles_;
    std::vector<DebugRect> rects_;
    std::vector<DebugText> texts_;
};
} // namespace rs::core

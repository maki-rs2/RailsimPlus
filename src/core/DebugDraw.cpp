#include "DebugDraw.h"

namespace rs::core {

void DebugDrawList::Clear() {
    polylines_.clear();
    circles_.clear();
    rects_.clear();
    texts_.clear();
}

void DebugDrawList::AddLine(const Vec3& a, const Vec3& b, const DebugStyle& style) {
    const Vec3 points[2] = {a, b};
    AddPolyline(points, 2, false, style);
}

void DebugDrawList::AddPolyline(const Vec3* points, size_t count, bool closed,
                                const DebugStyle& style) {
    if (points == nullptr || count < 2) {
        return;
    }
    DebugPolyline value;
    value.points.assign(points, points + count);
    value.closed = closed;
    value.style = style;
    polylines_.push_back(std::move(value));
}

void DebugDrawList::AddCircle(const Vec3& center, float radiusPx, const DebugStyle& style) {
    if (radiusPx > 0.0f) {
        circles_.push_back({center, radiusPx, style});
    }
}

void DebugDrawList::AddRect(const Vec3& center, float halfWidthPx, float halfHeightPx,
                            const DebugStyle& style) {
    if (halfWidthPx > 0.0f && halfHeightPx > 0.0f) {
        rects_.push_back({center, halfWidthPx, halfHeightPx, style});
    }
}

void DebugDrawList::AddText(const Vec3& anchor, const Vec2& offsetPx, std::string_view utf8,
                            const DebugStyle& style) {
    if (utf8.empty()) {
        return;
    }
    // 文字列はUTF-8のまま保持する。
    // グリフ展開は描画層で行う。
    texts_.push_back({anchor, offsetPx, std::string(utf8), style});
}

} // namespace rs::core

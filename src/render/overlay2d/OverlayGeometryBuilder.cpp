#include "OverlayGeometryBuilder.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <numbers>

namespace rs::render {
namespace {

constexpr float kMinClipW = 1.0e-5f;
constexpr float kMinSegmentLengthPx = 1.0e-4f;
constexpr float kMiterLimit = 4.0f;
constexpr uint32_t kMinCircleSegments = 12;
constexpr uint32_t kMaxCircleSegments = 96;

struct ClipPoint { float x; float y; float z; float w; };
struct ScreenPoint { float x; float y; float ndcZ; float viewDistance; };
struct Vec2 { float x; float y; };

ClipPoint TransformToClip(const rs::core::Vec3& point, const rs::core::Mat4& viewProj) {
    const float input[4] = {point.x, point.y, point.z, 1.0f};
    float output[4] = {};
    rs::core::TransformPoint4(viewProj, input, output);
    return {output[0], output[1], output[2], output[3]};
}

ClipPoint Interpolate(const ClipPoint& a, const ClipPoint& b, float t) {
    return {std::lerp(a.x, b.x, t), std::lerp(a.y, b.y, t),
            std::lerp(a.z, b.z, t), std::lerp(a.w, b.w, t)};
}

bool ClipToNear(ClipPoint& a, ClipPoint& b) {
    const bool aVisible = a.w > kMinClipW;
    const bool bVisible = b.w > kMinClipW;
    if (!aVisible && !bVisible) return false;
    if (aVisible && bVisible) return true;
    const float t = std::clamp((kMinClipW - a.w) / (b.w - a.w), 0.0f, 1.0f);
    const ClipPoint clipped = Interpolate(a, b, t);
    if (aVisible) b = clipped;
    else a = clipped;
    return true;
}

ScreenPoint ToScreen(const ClipPoint& point, const OverlayGeometryConfig& config) {
    const float ndcX = point.x / point.w;
    const float ndcY = point.y / point.w;
    return {(ndcX * 0.5f + 0.5f) * static_cast<float>(config.width),
            (1.0f - (ndcY * 0.5f + 0.5f)) * static_cast<float>(config.height),
            point.z / point.w, std::fabs(point.w)};
}

bool ProjectVisible(const rs::core::Vec3& point, const rs::core::Mat4& viewProj,
                    const OverlayGeometryConfig& config, ScreenPoint& output) {
    const ClipPoint clip = TransformToClip(point, viewProj);
    if (clip.w <= kMinClipW) return false;
    output = ToScreen(clip, config);
    return true;
}

Vec2 Subtract(const ScreenPoint& a, const ScreenPoint& b) { return {a.x - b.x, a.y - b.y}; }
float Length(const Vec2& value) { return std::hypot(value.x, value.y); }
Vec2 Normalize(const Vec2& value) {
    const float length = Length(value);
    if (length <= kMinSegmentLengthPx) return {};
    return {value.x / length, value.y / length};
}
Vec2 Perpendicular(const Vec2& value) { return {-value.y, value.x}; }
float Dot(const Vec2& a, const Vec2& b) { return a.x * b.x + a.y * b.y; }

uint32_t ApplyAlpha(uint32_t color, float scale) {
    const uint32_t alpha = (color >> 24u) & 0xFFu;
    const uint32_t adjusted = static_cast<uint32_t>(std::lround(
        static_cast<float>(alpha) * std::clamp(scale, 0.0f, 1.0f)));
    return (color & 0x00FFFFFFu) | (std::min(adjusted, 255u) << 24u);
}

float DashPeriodPx(const rs::core::DebugStyle& style) {
    if (style.dash == rs::core::DebugDashStyle::Solid) return 0.0f;
    return std::max(style.dashPeriodPx, 0.0f);
}

float DashDuty(const rs::core::DebugStyle& style, const OverlayGeometryConfig& config) {
    if (style.dash == rs::core::DebugDashStyle::Solid) return 1.0f;
    const float duty = std::clamp(config.dashDutyCycle, 0.0f, 1.0f);
    return style.dash == rs::core::DebugDashStyle::Dotted ? duty * 0.5f : duty;
}

float Attenuation(const ScreenPoint& point, const rs::core::DebugStyle& style,
                  const OverlayGeometryConfig& config) {
    if (!style.distanceAttenuation) return 1.0f;
    return OverlayGeometryBuilder::DistanceAttenuation(point.viewDistance, config);
}

OverlayVertex MakeVertex(float x, float y, const ScreenPoint& point, float acrossWidth,
                         float alongPx, uint32_t color, const rs::core::DebugStyle& style,
                         const OverlayGeometryConfig& config) {
    return {x, y, point.ndcZ, acrossWidth, alongPx, DashPeriodPx(style), DashDuty(style, config), color};
}

void AddQuad(std::vector<OverlayVertex>& output, const ScreenPoint& a, const ScreenPoint& b,
             const Vec2& offsetA, const Vec2& offsetB, float alongA, float alongB,
             const rs::core::DebugStyle& style, const OverlayGeometryConfig& config) {
    const float scaleA = Attenuation(a, style, config);
    const float scaleB = Attenuation(b, style, config);
    const uint32_t colorA = ApplyAlpha(style.color, scaleA);
    const uint32_t colorB = ApplyAlpha(style.color, scaleB);
    const Vec2 scaledOffsetA = {offsetA.x * scaleA, offsetA.y * scaleA};
    const Vec2 scaledOffsetB = {offsetB.x * scaleB, offsetB.y * scaleB};
    const OverlayVertex vertices[6] = {
        MakeVertex(a.x + scaledOffsetA.x, a.y + scaledOffsetA.y, a, 1.0f, alongA, colorA, style, config),
        MakeVertex(a.x - scaledOffsetA.x, a.y - scaledOffsetA.y, a, -1.0f, alongA, colorA, style, config),
        MakeVertex(b.x - scaledOffsetB.x, b.y - scaledOffsetB.y, b, -1.0f, alongB, colorB, style, config),
        MakeVertex(a.x + scaledOffsetA.x, a.y + scaledOffsetA.y, a, 1.0f, alongA, colorA, style, config),
        MakeVertex(b.x - scaledOffsetB.x, b.y - scaledOffsetB.y, b, -1.0f, alongB, colorB, style, config),
        MakeVertex(b.x + scaledOffsetB.x, b.y + scaledOffsetB.y, b, 1.0f, alongB, colorB, style, config),
    };
    output.insert(output.end(), std::begin(vertices), std::end(vertices));
}

void AddBevel(std::vector<OverlayVertex>& output, const ScreenPoint& point, const Vec2& previous,
              const Vec2& next, float alongPx, const rs::core::DebugStyle& style,
              const OverlayGeometryConfig& config) {
    const uint32_t color = ApplyAlpha(style.color, Attenuation(point, style, config));
    const OverlayVertex center = MakeVertex(point.x, point.y, point, 0.0f, alongPx, color, style, config);
    const OverlayVertex vertices[6] = {
        center, MakeVertex(point.x + previous.x, point.y + previous.y, point, 1.0f, alongPx, color, style, config),
        MakeVertex(point.x + next.x, point.y + next.y, point, 1.0f, alongPx, color, style, config),
        center, MakeVertex(point.x - next.x, point.y - next.y, point, -1.0f, alongPx, color, style, config),
        MakeVertex(point.x - previous.x, point.y - previous.y, point, -1.0f, alongPx, color, style, config),
    };
    output.insert(output.end(), std::begin(vertices), std::end(vertices));
}

void ExpandPolyline(std::vector<OverlayVertex>& output, const std::vector<ScreenPoint>& points,
                    bool closed, const rs::core::DebugStyle& style,
                    const OverlayGeometryConfig& config) {
    if (points.size() < 2 || style.widthPx <= 0.0f) return;
    const size_t pointCount = points.size();
    const size_t segmentCount = closed ? pointCount : pointCount - 1;
    std::vector<Vec2> directions(segmentCount);
    std::vector<Vec2> starts(segmentCount);
    std::vector<Vec2> ends(segmentCount);
    std::vector<float> lengths(segmentCount);
    std::vector<float> along(pointCount, 0.0f);
    std::vector<bool> bevel(pointCount, false);
    const float halfWidth = style.widthPx * 0.5f;

    for (size_t segment = 0; segment < segmentCount; ++segment) {
        const size_t next = (segment + 1) % pointCount;
        directions[segment] = Normalize(Subtract(points[next], points[segment]));
        lengths[segment] = Length(Subtract(points[next], points[segment]));
        const Vec2 normal = Perpendicular(directions[segment]);
        starts[segment] = {normal.x * halfWidth, normal.y * halfWidth};
        ends[segment] = starts[segment];
        if (segment + 1 < pointCount) along[segment + 1] = along[segment] + lengths[segment];
    }

    const size_t firstJoin = closed ? 0 : 1;
    const size_t endJoin = closed ? pointCount : pointCount - 1;
    for (size_t point = firstJoin; point < endJoin; ++point) {
        const size_t previous = (point + segmentCount - 1) % segmentCount;
        const size_t next = point % segmentCount;
        if (lengths[previous] <= kMinSegmentLengthPx ||
            lengths[next] <= kMinSegmentLengthPx) {
            continue;
        }
        const Vec2 previousNormal = Perpendicular(directions[previous]);
        const Vec2 nextNormal = Perpendicular(directions[next]);
        const Vec2 miterDirection = Normalize({previousNormal.x + nextNormal.x,
                                                previousNormal.y + nextNormal.y});
        const float denominator = Dot(miterDirection, nextNormal);
        const float miterLength = std::fabs(denominator) > kMinSegmentLengthPx
                                      ? halfWidth / denominator : kMiterLimit * halfWidth + 1.0f;
        if (std::fabs(miterLength) <= kMiterLimit * halfWidth) {
            const Vec2 miter = {miterDirection.x * miterLength, miterDirection.y * miterLength};
            ends[previous] = miter;
            starts[next] = miter;
        } else {
            bevel[point] = true;
        }
    }

    for (size_t segment = 0; segment < segmentCount; ++segment) {
        if (lengths[segment] <= kMinSegmentLengthPx) continue;
        const size_t next = (segment + 1) % pointCount;
        const float alongEnd = closed && next == 0
                                   ? along[segment] + lengths[segment]
                                   : along[next];
        AddQuad(output, points[segment], points[next], starts[segment], ends[segment], along[segment],
                alongEnd, style, config);
    }
    for (size_t point = firstJoin; point < endJoin; ++point) {
        if (!bevel[point]) continue;
        const size_t previous = (point + segmentCount - 1) % segmentCount;
        const size_t next = point % segmentCount;
        AddBevel(output, points[point], ends[previous], starts[next], along[point], style, config);
    }
}

// 線分の両端が画面の同じ側の外にあれば、線分は丸ごと画面の外にある。太さ・ミター・縁のぼかしではみ出す分を
// 余白(ピクセル)として見込む。
bool IsSegmentOffScreen(const ClipPoint& a, const ClipPoint& b, float marginPx,
                        const OverlayGeometryConfig& config) {
    const float marginX = marginPx * 2.0f / static_cast<float>(config.width);
    const float marginY = marginPx * 2.0f / static_cast<float>(config.height);
    const float limitAX = a.w * (1.0f + marginX), limitBX = b.w * (1.0f + marginX);
    const float limitAY = a.w * (1.0f + marginY), limitBY = b.w * (1.0f + marginY);
    return (a.x > limitAX && b.x > limitBX) || (a.x < -limitAX && b.x < -limitBX) ||
           (a.y > limitAY && b.y > limitBY) || (a.y < -limitAY && b.y < -limitBY);
}

void ExpandWorldPolyline(std::vector<OverlayVertex>& output, const rs::core::DebugPolyline& line,
                         const rs::core::Mat4& viewProj, const OverlayGeometryConfig& config) {
    std::vector<ScreenPoint> run;
    // 破線は線の始点からの距離で位相が決まる。途中を捨てると位相がずれて点滅するので、実線だけ捨てる。
    const bool cullOffScreen = line.style.dash == rs::core::DebugDashStyle::Solid;
    const float marginPx = line.style.widthPx * kMiterLimit * 0.5f + config.aaSmoothingWidthPx + 2.0f;
    const size_t segmentCount = line.closed ? line.points.size() : line.points.size() - 1;
    for (size_t segment = 0; segment < segmentCount; ++segment) {
        ClipPoint a = TransformToClip(line.points[segment], viewProj);
        ClipPoint b = TransformToClip(line.points[(segment + 1) % line.points.size()], viewProj);
        if (!ClipToNear(a, b) || (cullOffScreen && IsSegmentOffScreen(a, b, marginPx, config))) {
            if (run.size() >= 2) ExpandPolyline(output, run, false, line.style, config);
            run.clear();
            continue;
        }
        const ScreenPoint screenA = ToScreen(a, config);
        const ScreenPoint screenB = ToScreen(b, config);
        if (run.empty()) run.push_back(screenA);
        else if (std::fabs(run.back().x - screenA.x) > kMinSegmentLengthPx ||
                 std::fabs(run.back().y - screenA.y) > kMinSegmentLengthPx) {
            if (run.size() >= 2) ExpandPolyline(output, run, false, line.style, config);
            run.clear();
            run.push_back(screenA);
        }
        run.push_back(screenB);
    }
    if (run.size() >= 2) ExpandPolyline(output, run, line.closed && run.size() == line.points.size(),
                                         line.style, config);
}

void AddFilledPolygon(std::vector<OverlayVertex>& output, const std::vector<ScreenPoint>& points,
                      const ScreenPoint& center, const rs::core::DebugStyle& style,
                      const OverlayGeometryConfig& config) {
    const uint32_t color = ApplyAlpha(style.color, Attenuation(center, style, config));
    float alongPx = 0.0f;
    for (size_t point = 0; point < points.size(); ++point) {
        const size_t next = (point + 1) % points.size();
        const float edgeLength = Length(Subtract(points[next], points[point]));
        const OverlayVertex vertices[3] = {
            MakeVertex(center.x, center.y, center, 0.0f, 0.0f, color, style, config),
            MakeVertex(points[point].x, points[point].y, points[point], 0.0f, alongPx, color, style, config),
            MakeVertex(points[next].x, points[next].y, points[next], 0.0f, alongPx + edgeLength, color, style, config),
        };
        output.insert(output.end(), std::begin(vertices), std::end(vertices));
        alongPx += edgeLength;
    }
}

std::vector<ScreenPoint> MakeCircle(const ScreenPoint& center, float radiusPx, uint32_t segments) {
    std::vector<ScreenPoint> points;
    points.reserve(segments);
    for (uint32_t segment = 0; segment < segments; ++segment) {
        const float angle = (2.0f * std::numbers::pi_v<float>) * static_cast<float>(segment) /
                            static_cast<float>(segments);
        points.push_back({center.x + std::cos(angle) * radiusPx,
                          center.y + std::sin(angle) * radiusPx, center.ndcZ, center.viewDistance});
    }
    return points;
}

std::vector<OverlayVertex>& OutputFor(OverlayGeometry& geometry, const rs::core::DebugStyle& style) {
    return style.depthMode == rs::core::DebugDepthMode::AlwaysOnTop
               ? geometry.alwaysOnTop : geometry.depthTested;
}

std::vector<OverlayTextVertex>& TextOutputFor(OverlayGeometry& geometry, const rs::core::DebugStyle& style) {
    return style.depthMode == rs::core::DebugDepthMode::AlwaysOnTop ? geometry.textAlwaysOnTop : geometry.textDepthTested;
}

void AddTextQuad(std::vector<OverlayTextVertex>& output, float x, float y, const ScreenPoint& anchor,
                 const GlyphInfo& glyph, uint32_t color) {
    const float right = x + glyph.widthPx;
    const float bottom = y + glyph.heightPx;
    const OverlayTextVertex topLeft = {x, y, anchor.ndcZ, glyph.u0, glyph.v0, color};
    const OverlayTextVertex topRight = {right, y, anchor.ndcZ, glyph.u1, glyph.v0, color};
    const OverlayTextVertex bottomRight = {right, bottom, anchor.ndcZ, glyph.u1, glyph.v1, color};
    const OverlayTextVertex bottomLeft = {x, bottom, anchor.ndcZ, glyph.u0, glyph.v1, color};
    const OverlayTextVertex vertices[6] = {
        topLeft, topRight, bottomRight,
        topLeft, bottomRight, bottomLeft,
    };
    output.insert(output.end(), std::begin(vertices), std::end(vertices));
}

} // namespace

uint32_t OverlayGeometryBuilder::CircleSegmentCount(float radiusPx) {
    const uint32_t requested = static_cast<uint32_t>(std::ceil(std::max(radiusPx, 0.0f) * 0.75f));
    return std::clamp(requested, kMinCircleSegments, kMaxCircleSegments);
}

float OverlayGeometryBuilder::DistanceAttenuation(float distance, const OverlayGeometryConfig& config) {
    if (config.attenuationReferenceDistance <= 0.0f) return 1.0f;
    const float scale = config.attenuationReferenceDistance / std::max(distance, 1.0f);
    return std::clamp(scale, config.attenuationMinScale, 1.0f);
}

OverlayGeometry OverlayGeometryBuilder::Build(const rs::core::DebugDrawList& list,
                                              const rs::core::Mat4& viewProj,
                                              const OverlayGeometryConfig& config,
                                              GlyphProvider* glyphProvider) {
    OverlayGeometry geometry;
    Build(list, viewProj, config, glyphProvider, geometry);
    return geometry;
}

void OverlayGeometryBuilder::Build(const rs::core::DebugDrawList& list, const rs::core::Mat4& viewProj,
                                   const OverlayGeometryConfig& config, GlyphProvider* glyphProvider,
                                   OverlayGeometry& geometry) {
    geometry.depthTested.clear();
    geometry.alwaysOnTop.clear();
    geometry.textDepthTested.clear();
    geometry.textAlwaysOnTop.clear();
    if (config.width == 0 || config.height == 0) return;
    for (const rs::core::DebugPolyline& line : list.Polylines()) {
        ExpandWorldPolyline(OutputFor(geometry, line.style), line, viewProj, config);
    }
    for (const rs::core::DebugCircle& circle : list.Circles()) {
        ScreenPoint center;
        if (!ProjectVisible(circle.center, viewProj, config, center)) continue;
        const std::vector<ScreenPoint> points = MakeCircle(center, circle.radiusPx,
                                                            CircleSegmentCount(circle.radiusPx));
        std::vector<OverlayVertex>& output = OutputFor(geometry, circle.style);
        if (circle.style.widthPx <= 0.0f) {
            AddFilledPolygon(output, points, center, circle.style, config);
        } else {
            ExpandPolyline(output, points, true, circle.style, config);
        }
    }
    for (const rs::core::DebugRect& rect : list.Rects()) {
        ScreenPoint center;
        if (!ProjectVisible(rect.center, viewProj, config, center)) continue;
        const std::vector<ScreenPoint> points = {
            {center.x - rect.halfWidthPx, center.y - rect.halfHeightPx,
             center.ndcZ, center.viewDistance},
            {center.x + rect.halfWidthPx, center.y - rect.halfHeightPx,
             center.ndcZ, center.viewDistance},
            {center.x + rect.halfWidthPx, center.y + rect.halfHeightPx,
             center.ndcZ, center.viewDistance},
            {center.x - rect.halfWidthPx, center.y + rect.halfHeightPx,
             center.ndcZ, center.viewDistance},
        };
        std::vector<OverlayVertex>& output = OutputFor(geometry, rect.style);
        if (rect.style.widthPx <= 0.0f) {
            AddFilledPolygon(output, points, center, rect.style, config);
        } else {
            ExpandPolyline(output, points, true, rect.style, config);
        }
    }
    if (glyphProvider != nullptr) {
        for (const rs::core::DebugText& text : list.Texts()) {
            ScreenPoint anchor;
            if (!ProjectVisible(text.anchor, viewProj, config, anchor)) continue;
            float penX = anchor.x + text.offsetPx.x;
            const float baselineY = anchor.y + text.offsetPx.y;
            std::vector<OverlayTextVertex>& output = TextOutputFor(geometry, text.style);
            for (uint32_t codePoint : DecodeUtf8(text.utf8)) {
                GlyphInfo glyph;
                if (!glyphProvider->GetGlyph(codePoint, glyph)) continue;
                AddTextQuad(output, penX + glyph.bearingXPx, baselineY - glyph.bearingYPx, anchor, glyph,
                            ApplyAlpha(text.style.color, Attenuation(anchor, text.style, config)));
                penX += glyph.advancePx;
            }
        }
    }
}

} // namespace rs::render

#include "ViewportSectionOverlay.h"

#include "imgui.h"

#include <cmath>
#include <cstdio>

#include "../../editor/tool/Editor.h"
#include "../../editor/tool/EditorSectionEdit.h"
#include "../../core/DebugDraw.h"
#include "../../world/alignment/TrackTraversal.h"
#include "../gpu/App.h"
#include "OverlayColorTuning.h"
#include "OverlayLineTuning.h"

namespace rs::app {

namespace {

// ワールド座標をスクリーン座標へ投影し、可視(near/far/画面外でない)なら true を返す。
// ViewportOverlay.cppのProjectVisibleと同じ形(あちらは無名namespaceのため共有できない)。
bool ProjectVisible(const rs::core::Vec3& p, const rs::app::ViewProjResult& vpr, UINT width,
                    UINT height, ImVec2& out) {
    const rs::core::ProjectedPoint pp = rs::core::ProjectToScreen(p, vpr.viewProj, width, height);
    if (!pp.Visible()) return false;
    out = ImVec2(pp.x, pp.y);
    return true;
}

} // namespace

std::vector<SectionBoundaryPoint> CollectSectionBoundaries(const rs::world::TrackGraph& graph,
                                                           const rs::world::Guid& edge,
                                                           rs::editor::SectionLayer layer) {
    std::vector<SectionBoundaryPoint> result;
    const auto it = graph.edges.find(edge);
    if (it == graph.edges.end() || it->second.spline.empty()) return result;
    const rs::world::TrackEdge& e = it->second;
    const std::vector<rs::editor::EditorSectionEdit::BoundaryS> boundaries =
        rs::editor::EditorSectionEdit::CollectBoundaries(e, layer);
    result.reserve(boundaries.size());
    for (const rs::editor::EditorSectionEdit::BoundaryS& b : boundaries) {
        const rs::world::TrackFrame frame = rs::world::EvaluateAt(e, b.s, +1);
        SectionBoundaryPoint point;
        point.index = b.index;
        point.s = b.s;
        point.world = frame.pos + frame.up * e.railTopLift;
        result.push_back(point);
    }
    return result;
}

bool PickSectionBoundary(const std::vector<SectionBoundaryPoint>& points,
                         const rs::app::ViewProjResult& vpr, UINT width, UINT height,
                         float screenX, float screenY, float pxRadius, std::size_t& outIndex) {
    float bestDist = pxRadius;
    bool found = false;
    for (const SectionBoundaryPoint& point : points) {
        ImVec2 screen;
        if (!ProjectVisible(point.world, vpr, width, height, screen)) continue;
        const float dx = screenX - screen.x;
        const float dy = screenY - screen.y;
        const float dist = std::sqrt(dx * dx + dy * dy);
        if (dist <= bestDist) {
            bestDist = dist;
            outIndex = point.index;
            found = true;
        }
    }
    return found;
}

void DrawSectionOverlay(const EditorViewportContext& ctx, const rs::app::ViewProjResult& vpr) {
    rs::editor::Editor& gEditor = ctx.editor;
    if (!gEditor.sectionSplitMode && !gEditor.sectionEditMode) return;
    if (gEditor.SectionTargetEdge() == rs::world::NilGuid()) return;
    const auto edgeIt = gEditor.graph.edges.find(gEditor.SectionTargetEdge());
    if (edgeIt == gEditor.graph.edges.end() || edgeIt->second.spline.empty()) return;
    const rs::world::TrackEdge& edge = edgeIt->second;
    const OverlayColorTuning& colors = OverlayColorTuning::Embedded();
    const UINT width = ctx.width;
    const UINT height = ctx.height;
    auto projVisible = [&](const rs::core::Vec3& p, ImVec2& out) -> bool {
        return ProjectVisible(p, vpr, width, height, out);
    };

    const std::size_t selectedIndex = gEditor.SelectedSectionIndex(gEditor.sectionLayer);
    const std::vector<rs::editor::EditorSectionEdit::BoundaryS> boundaries =
        rs::editor::EditorSectionEdit::CollectBoundaries(edge, gEditor.sectionLayer);
    const double edgeLen = static_cast<double>(edge.spline.back().s);
    const auto drawSpan = [&](double sectionStart, double sectionEnd, std::uint32_t color) {
        std::vector<rs::core::Vec3> points;
        const rs::world::TrackFrame first = rs::world::EvaluateAt(edge, sectionStart, +1);
        points.push_back(first.pos + first.up * edge.railTopLift);
        for (const rs::world::TrackNodeSample& sample : edge.spline) {
            const double s = static_cast<double>(sample.s);
            if (s <= sectionStart || s >= sectionEnd) continue;
            points.push_back(sample.pos + sample.up * edge.railTopLift);
        }
        const rs::world::TrackFrame last = rs::world::EvaluateAt(edge, sectionEnd, +1);
        points.push_back(last.pos + last.up * edge.railTopLift);
        rs::core::DebugStyle style;
        style.color = color;
        style.widthPx = OverlayLineTuning::Embedded().sectionLineWidthPx;
        ctx.app.DebugDrawList().AddPolyline(points.data(), points.size(), false, style);
    };
    const std::uint32_t sectionColors[] = {colors.sectionLine, colors.layingLine,
                                           colors.valid, colors.branchMarker};
    if (gEditor.sectionEditMode) {
        for (std::size_t i = 0; i < edge.pluginSections.SpanCount(); ++i) {
            const std::vector<rs::world::IntervalList<rs::world::PluginSet>::Span>& spans =
                edge.pluginSections.Spans();
            const double start = spans[i].sStart;
            const double end = i + 1 < spans.size() ? spans[i + 1].sStart : edgeLen;
            const std::uint32_t color = i == selectedIndex
                ? colors.select : sectionColors[i % std::size(sectionColors)];
            drawSpan(start, end, color);
        }
    } else if (selectedIndex < edge.pluginSections.SpanCount()) {
        const std::vector<rs::world::IntervalList<rs::world::PluginSet>::Span>& spans =
            edge.pluginSections.Spans();
        const double start = spans[selectedIndex].sStart;
        const double end = selectedIndex + 1 < spans.size() ? spans[selectedIndex + 1].sStart : edgeLen;
        drawSpan(start, end, colors.select);
    }

    // ---- 境界マーカー ----
    const std::vector<SectionBoundaryPoint> points =
        CollectSectionBoundaries(gEditor.graph, gEditor.SectionTargetEdge(), gEditor.sectionLayer);
    for (const SectionBoundaryPoint& point : points) {
        const bool isDragging =
            gEditor.sectionEditUi.boundaryDragActive && gEditor.sectionEditUi.boundaryDragIndex == point.index;
        rs::core::DebugStyle fillStyle;
        fillStyle.color = isDragging ? colors.drag : colors.edgeSelected;
        fillStyle.widthPx = 0.0f;
        ctx.app.DebugDrawList().AddCircle(point.world, OverlayLineTuning::Embedded().dotRadiusPx, fillStyle);
        rs::core::DebugStyle ringStyle;
        ringStyle.color = colors.sectionRing;
        ringStyle.widthPx = OverlayLineTuning::Embedded().minWidthPx;
        ctx.app.DebugDrawList().AddCircle(point.world, OverlayLineTuning::Embedded().selectRingRadiusPx, ringStyle);
    }

    // ---- 分割位置プレビュー(横線+s値テキスト) ----
    auto drawSMarker = [&](double s, ImU32 col) {
        const rs::world::TrackFrame f = rs::world::EvaluateAt(edge, s, +1);
        const rs::core::Vec3 center = f.pos + f.up * edge.railTopLift;
        const rs::core::Vec3 right = rs::core::Vec3::Cross(f.up, f.tangent);
        const rs::core::Vec3 a = center - right * 2.0f;
        const rs::core::Vec3 b = center + right * 2.0f;
        rs::core::DebugStyle style;
        style.color = col;
        style.widthPx = 3.0f;
        ctx.app.DebugDrawList().AddLine(a, b, style);
        char buf[32];
        std::snprintf(buf, sizeof(buf), "s=%.2f", s);
        rs::core::DebugStyle textStyle;
        textStyle.color = col;
        textStyle.widthPx = 0.0f;
        ctx.app.DebugDrawList().AddText(center, rs::core::Vec2(8.0f, -8.0f), buf, textStyle);
    };
    if (gEditor.sectionSplitMode && gEditor.sectionEditUi.splitHoverValid &&
        !gEditor.sectionEditUi.boundaryDragActive) {
        drawSMarker(gEditor.sectionEditUi.splitHoverS, colors.select);
    }
    if (gEditor.sectionEditUi.boundaryDragActive) {
        drawSMarker(gEditor.sectionEditUi.boundaryDragPreviewS, colors.drag);
    }
}

} // namespace rs::app

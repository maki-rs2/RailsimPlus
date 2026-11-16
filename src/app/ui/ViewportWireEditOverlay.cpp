//
// ViewportWireEditOverlay.cpp - 架線編集モードの画面表示(接続点の丸、径間のホバー・選択)
//
#include "ViewportWireEditOverlay.h"

#include "../../core/DebugDraw.h"
#include "../../core/Math.h"
#include "../../editor/tool/Editor.h"
#include "../gpu/App.h"
#include "OverlayColorTuning.h"
#include "OverlayLineTuning.h"
#include "ViewportOverlayGeometry.h"

namespace rs::app {

namespace {

rs::core::DebugStyle MakeFillStyle(uint32_t color) {
    rs::core::DebugStyle style;
    style.color = color;
    style.widthPx = 0.0f;
    style.depthMode = rs::core::DebugDepthMode::AlwaysOnTop;
    return style;
}

void DrawSpanLine(const EditorViewportContext& ctx, const rs::editor::WireSpanRef& ref,
                  uint32_t color) {
    for (const WireSpanCacheEntry& span : ctx.wireEditCache.spans) {
        if (!rs::editor::SameWireSpan(span.ref, ref)) continue;
        ctx.app.DebugDrawList().AddLine(span.pos1, span.pos2, MakeNodeLineStyle(color, OverlayLineTuning::Embedded().nodeLineWidthPx));
        return;
    }
}

} // namespace

void DrawWireEditOverlay(const EditorViewportContext& ctx) {
    const rs::editor::Editor& editor = ctx.editor;
    const OverlayColorTuning& colors = OverlayColorTuning::Embedded();
    if (!editor.wireEditMode) return;
    for (const WireConnectPoint& point : ctx.wireEditCache.points) {
        const bool isFirst =
            editor.wireFirstPoint.has_value() && rs::editor::SameWirePoint(point.ref, *editor.wireFirstPoint);
        const bool isHovered = editor.wireHoveredPoint.has_value() &&
                               rs::editor::SameWirePoint(point.ref, *editor.wireHoveredPoint);
        const uint32_t color = isFirst ? colors.select : isHovered ? colors.hover : colors.pointIdle;
        ctx.app.DebugDrawList().AddCircle(point.pos, isHovered ? OverlayLineTuning::Embedded().nodeHoverRadiusPx : OverlayLineTuning::Embedded().nodeRadiusPx, MakeFillStyle(color));
        if (isFirst) ctx.app.DebugDrawList().AddCircle(point.pos, OverlayLineTuning::Embedded().selectRingRadiusPx, MakeNodeLineStyle(colors.select, OverlayLineTuning::Embedded().nodeLineWidthPx));
    }
    if (editor.wireHoveredSpan.has_value()) DrawSpanLine(ctx, *editor.wireHoveredSpan, colors.hover);
    if (editor.wireSelectedSpan.has_value()) DrawSpanLine(ctx, *editor.wireSelectedSpan, colors.select);
}

} // namespace rs::app

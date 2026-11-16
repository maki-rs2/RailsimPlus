#include "ViewportOverlayGeometry.h"

#include "../../core/Epsilon.h"

#include <algorithm>
#include <cstdio>

#include "../../core/DebugDraw.h"
#include "../../core/Math.h"
#include "../../editor/tool/Editor.h"
#include "../../world/alignment/Alignment.h"
#include "../action/TrackLayingPreviewColor.h"
#include "../gpu/App.h"
#include "../plugin_session/RailTieHeights.h"
#include "OverlayColorTuning.h"
#include "OverlayLineTuning.h"

namespace rs::app {
namespace {
// 注記の線の最低の太さ[px]。細い線はすべてこの太さにそろえる(OverlayLineTuning)。
float MinLineWidthPx() { return OverlayLineTuning::Embedded().minWidthPx; }

void AddPolyline(const EditorViewportContext& ctx, const std::vector<rs::core::Vec3>& points,
                 uint32_t color, float width) {
    ctx.app.DebugDrawList().AddPolyline(points.data(), points.size(), false,
                                        MakeTopStyle(color, width));
}
// ノードに関する線(選択の輪・接線ハンドル・ドラッグ中のガイドなど)の太さの倍率。
constexpr float kNodeLineWidthScale = 2.0f;

// 複線の基準curve以外のtrackを描くときの不透明度(基準curveより薄くする)。
constexpr uint32_t kSidePolylineAlpha = 120;

// 区間の種類(直線/緩和曲線/円弧)ごとに色を変えて点列を描く。kindsが点列と同数でなければ
// 単色(緑)。upsを渡すと各点を上方向へliftだけ持ち上げる。alphaOverrideが0以外なら
// 色の不透明度をその値にする。
void AddKindPolyline(const EditorViewportContext& ctx, const std::vector<rs::core::Vec3>& points,
                     const std::vector<rs::world::AlignmentSegment::Kind>& kinds,
                     const std::vector<rs::core::Vec3>* ups, float lift, float width,
                     uint32_t alphaOverride) {
    const bool haveKinds = kinds.size() == points.size();
    const bool haveUp = ups != nullptr && ups->size() == points.size();
    const auto withAlpha = [alphaOverride](uint32_t c) {
        return alphaOverride == 0 ? c : ((c & 0x00FFFFFFu) | (alphaOverride << 24));
    };
    std::vector<rs::core::Vec3> segment;
    uint32_t color = withAlpha(OverlayColorTuning::Embedded().valid);
    for (size_t index = 0; index < points.size(); ++index) {
        const uint32_t nextColor = haveKinds ? withAlpha(PreviewColor(kinds[index])) : color;
        if (!segment.empty() && nextColor != color) { AddPolyline(ctx, segment, color, width); segment.clear(); }
        color = nextColor;
        segment.push_back(haveUp ? points[index] + (*ups)[index] * lift : points[index]);
    }
    if (!segment.empty()) AddPolyline(ctx, segment, color, width);
}
void DrawRadiusText(const EditorViewportContext& ctx, const rs::core::Vec3& point, float radius) {
    char text[32];
    std::snprintf(text, sizeof(text), "R = %.1f", radius);
    rs::core::DebugStyle style = MakeTopStyle(OverlayColorTuning::Embedded().labelText, 0.0f);
    ctx.app.DebugDrawList().AddText(point,rs::core::Vec2(0.0f, 0.0f), text, style);
}

// ノード位置/接線ハンドルドラッグ中のedge形状プレビューを描画する。折れ線は
// BuildEdgeShape が作った spline そのまま(確定後の edge.spline と一致する)。
// preview.ok==falseの間は何も描かない(呼び出し側がガイド線・ハンドル自体をエラー色に変える)。
void DrawDragCurvePreview(const EditorViewportContext& ctx, const rs::editor::DragCurvePreview& preview) {
    if (!preview.ok) return;
    const uint32_t previewColor = OverlayColorTuning::Embedded().drag;
    for (const rs::editor::EdgeShapePreview& shape : preview.edges) {
        std::vector<rs::core::Vec3> polyline;
        polyline.reserve(shape.spline.size());
        for (const rs::world::TrackNodeSample& sample : shape.spline) polyline.push_back(sample.pos);
        AddPolyline(ctx, polyline, previewColor, MinLineWidthPx());
    }
}
} // namespace

// 点から基準面(Y=0)まで真下に伸びる線と、その高さの文字を描く。
// 地形(Surface)が無い間の「地面」は Y=0 の水平面とする。
void DrawHeightLine(const EditorViewportContext& ctx, const rs::core::Vec3& point) {
    const OverlayColorTuning& colors = OverlayColorTuning::Embedded();
    const uint32_t color = colors.labelStem;
    const rs::core::Vec3 foot(point.x, 0.0f, point.z);
    ctx.app.DebugDrawList().AddLine(point, foot, MakeTopStyle(color, MinLineWidthPx()));
    ctx.app.DebugDrawList().AddCircle(foot, OverlayLineTuning::Embedded().heightFootRadiusPx, MakeTopStyle(color, 0.0f));
    char text[32];
    std::snprintf(text, sizeof(text), "Y = %.1fm", point.y);
    ctx.app.DebugDrawList().AddText((point + foot) * 0.5f, rs::core::Vec2(8.0f, 0.0f), text,
                                    MakeTopStyle(colors.labelText, 0.0f));
}

// ノードの線用のスタイル。幅0(塗りつぶし)はそのまま、線は倍率をかけて、最低の太さを下回らないようにする。
// 遠くで薄くならないよう、距離による減衰は切る(ノードの操作は遠景でも見分けたい)。
rs::core::DebugStyle MakeNodeLineStyle(uint32_t color, float width) {
    const float scaledWidth = width > 0.0f ? std::max(width * kNodeLineWidthScale, MinLineWidthPx()) : width;
    rs::core::DebugStyle style = MakeTopStyle(color, scaledWidth);
    style.distanceAttenuation = false;
    return style;
}

void DrawTrackNodeGeometry(const EditorViewportContext& ctx) {
    rs::editor::Editor& editor = ctx.editor;
    const OverlayColorTuning& colors = OverlayColorTuning::Embedded();
    for (const std::pair<const rs::world::Guid, rs::world::TrackNode>& entry : editor.graph.nodes) {
        const rs::core::Vec3 point = editor.NodeRailTopPos(entry.second);
        const bool hovered = entry.first == editor.hoveredNode;
        const uint32_t color = hovered ? colors.hover : colors.pointIdle;
        ctx.app.DebugDrawList().AddCircle(point, hovered ? OverlayLineTuning::Embedded().nodeHoverRadiusPx : OverlayLineTuning::Embedded().nodeRadiusPx, MakeTopStyle(color, 0.0f));
        if (entry.first == editor.selectedNode)
            ctx.app.DebugDrawList().AddCircle(point, OverlayLineTuning::Embedded().selectRingRadiusPx, MakeNodeLineStyle(colors.select, OverlayLineTuning::Embedded().nodeLineWidthPx));
        if (ctx.layoutSession.warpLinks.Find(ctx.layoutSession.activeIndex, entry.first).has_value())
            ctx.app.DebugDrawList().AddRect(point, OverlayLineTuning::Embedded().markerRadiusPx, OverlayLineTuning::Embedded().markerRadiusPx,
                                            MakeNodeLineStyle(colors.layingAnchor, OverlayLineTuning::Embedded().nodeLineWidthPx));
    }
    if (editor.mode == rs::editor::ToolMode::Select && editor.nodeDragUi.active) {
        // 再ソルブ不成立(preview.ok==false)の間はガイド線・先端circleをエラー色にする
        const uint32_t guideColor = editor.nodeDragUi.preview.ok ? colors.drag : colors.invalid;
        const uint32_t tipColor = editor.nodeDragUi.preview.ok ? colors.drag : colors.invalid;
        const std::unordered_map<rs::world::Guid, rs::world::TrackNode, rs::world::GuidHash>::const_iterator nodeIt =
            editor.graph.nodes.find(editor.nodeDragUi.guid);
        if (nodeIt != editor.graph.nodes.end())
            ctx.app.DebugDrawList().AddLine(nodeIt->second.pos, editor.nodeDragUi.previewPos,
                                            MakeNodeLineStyle(guideColor, OverlayLineTuning::Embedded().guideLineWidthPx));
        ctx.app.DebugDrawList().AddCircle(editor.nodeDragUi.previewPos, OverlayLineTuning::Embedded().dragTipRadiusPx, MakeNodeLineStyle(tipColor, OverlayLineTuning::Embedded().nodeLineWidthPx));
        DrawDragCurvePreview(ctx, editor.nodeDragUi.preview);
    }

    // 接線ハンドル(選択中ノードのみ)。次数2(通過点)ノードは前後両方向、
    // 次数1(終端)は正方向のみ表示する。
    if (editor.selectedNode != rs::world::NilGuid() && editor.CanMoveNode(editor.selectedNode)) {
        const auto nodeIt = editor.graph.nodes.find(editor.selectedNode);
        // 接線ハンドルは次数1〜2のノードだけ(分岐の接線は変えられない)。
        if (nodeIt != editor.graph.nodes.end() && nodeIt->second.links.size() <= 2) {
            const rs::core::Vec3 basePos = editor.NodeRailTopPos(nodeIt->second);
            const bool dragging = editor.tangentDragUi.active && editor.tangentDragUi.guid == editor.selectedNode;
            rs::core::Vec3 dir = dragging ? editor.tangentDragUi.previewTangent : nodeIt->second.tangent;
            const float dirLen = dir.Length();
            if (dirLen > rs::core::kDirectionParallelEps) {
                const rs::core::Vec3 unitDir = dir * (1.0f / dirLen);
                const bool degreeTwo = nodeIt->second.links.size() == 2;
                const bool previewOk = !dragging || editor.tangentDragUi.preview.ok;
                const int sides[2] = {+1, degreeTwo ? -1 : 0};
                for (const int side : sides) {
                    if (side == 0) continue;
                    const bool draggingThisSide = dragging && editor.tangentDragUi.side == side;
                    const rs::core::Vec3 tip = basePos + unitDir * (rs::editor::kTangentHandleLengthM * static_cast<float>(side));
                    uint32_t handleColor = colors.valid;
                    if (draggingThisSide) handleColor = previewOk ? colors.drag : colors.invalid;
                    ctx.app.DebugDrawList().AddLine(basePos, tip, MakeNodeLineStyle(handleColor, OverlayLineTuning::Embedded().guideLineWidthPx));
                    const bool hoveredThisSide = editor.hoveredTangentHandleSide == side;
                    const float tipRadius = hoveredThisSide ? OverlayLineTuning::Embedded().dotHoverRadiusPx : OverlayLineTuning::Embedded().dotRadiusPx;
                    ctx.app.DebugDrawList().AddCircle(tip, tipRadius, MakeNodeLineStyle(handleColor, OverlayLineTuning::Embedded().nodeLineWidthPx));
                }
            }
            if (dragging) DrawDragCurvePreview(ctx, editor.tangentDragUi.preview);
        }
    }
}

namespace {

// edgeに近づいたときのマーカー。操作できる位置は color、できない位置は invalid の色で描く。
void DrawEdgeMarker(const EditorViewportContext& ctx, const rs::editor::Editor::EdgeMarker& marker,
                    uint32_t color) {
    if (!marker.visible) return;
    const OverlayColorTuning& colors = OverlayColorTuning::Embedded();
    const OverlayLineTuning& tuning = OverlayLineTuning::Embedded();
    const uint32_t drawColor = marker.allowed ? color : colors.invalid;
    ctx.app.DebugDrawList().AddCircle(marker.pos, tuning.markerRadiusPx,
                                      MakeNodeLineStyle(drawColor, tuning.nodeLineWidthPx));
    ctx.app.DebugDrawList().AddCircle(marker.pos, tuning.dotRadiusPx, MakeTopStyle(drawColor, 0.0f));
}

// 敷設ツールで点が置かれる位置の小さい円。edgeマーカーが出ていないときだけ描く。
void DrawSnapCursor(const EditorViewportContext& ctx, const rs::editor::Editor::SnapCursor& cursor) {
    if (!cursor.visible) return;
    const OverlayColorTuning& colors = OverlayColorTuning::Embedded();
    uint32_t color = colors.edgeMarkerIdle;
    if (cursor.kind == rs::editor::SnapKind::Parallel) color = colors.parallelSnapCursor;
    if (cursor.kind == rs::editor::SnapKind::Node) color = colors.valid;
    ctx.app.DebugDrawList().AddCircle(cursor.pos, OverlayLineTuning::Embedded().snapCursorRadiusPx,
                                      MakeTopStyle(color, 0.0f));
}

// Alt押下中は分割位置、敷設の始点待ちでSpaceを押していなければ分岐の始点を示す。
void DrawEdgeMarkers(const EditorViewportContext& ctx, const ViewProjResult& viewProj, float mouseX,
                     float mouseY) {
    const rs::editor::Editor& editor = ctx.editor;
    if (!ctx.input.AllowsPointer(rs::app::input::InputContext::View)) return;
    const OverlayColorTuning& colors = OverlayColorTuning::Embedded();
    if (ctx.input.MatchesPointer(rs::app::input::PointerAction::EdgeSplit)) {
        if (editor.CanAltSplit()) {
            DrawEdgeMarker(ctx, editor.FindSplitMarker(mouseX, mouseY, viewProj.viewProj, ctx.width, ctx.height),
                           colors.splitMarker);
        }
        return;
    }
    if (editor.layingSettings.snapDisabledHeld) return;
    DrawEdgeMarker(ctx, editor.FindBranchMarker(mouseX, mouseY, viewProj.viewProj, ctx.width, ctx.height),
                   colors.branchMarker);
    DrawSnapCursor(ctx, editor.FindSnapCursor(mouseX, mouseY, viewProj.viewProj, ctx.width, ctx.height));
}

}  // namespace

void DrawArcToolPreviewGeometry(const EditorViewportContext& ctx, const ViewProjResult& viewProj,
                                float mouseX, float mouseY) {
    rs::editor::Editor& editor = ctx.editor;
    const OverlayColorTuning& colors = OverlayColorTuning::Embedded();
    DrawEdgeMarkers(ctx, viewProj, mouseX, mouseY);
    if (editor.mode != rs::editor::ToolMode::PlaceArc) {
        ctx.lastArcPreview = rs::editor::PlacePreview{};
        return;
    }
    const auto drawNodes = [&](const std::vector<rs::world::Guid>& nodes, uint32_t color) {
        for (const rs::world::Guid& guid : nodes) {
            const std::unordered_map<rs::world::Guid, rs::world::TrackNode, rs::world::GuidHash>::const_iterator nodeIt =
                editor.graph.nodes.find(guid);
            if (nodeIt == editor.graph.nodes.end()) continue;
            const rs::core::Vec3 point = editor.NodeRailTopPos(nodeIt->second);
            ctx.app.DebugDrawList().AddCircle(point, OverlayLineTuning::Embedded().markerRadiusPx, MakeNodeLineStyle(color, OverlayLineTuning::Embedded().nodeLineWidthPx));
            ctx.app.DebugDrawList().AddCircle(point, OverlayLineTuning::Embedded().nodeRadiusPx, MakeTopStyle(color, 0.0f));
        }
    };
    drawNodes(editor.SelectedStartNodes(), colors.valid);
    drawNodes(editor.SelectedEndNodes(), colors.layingEndNode);
    rs::editor::PlacePreview preview = editor.BuildPreview(mouseX, mouseY, viewProj.viewProj, ctx.width, ctx.height);
    ctx.lastArcPreview = preview;
    if (!preview.active) return;
    // 始点・中間点・カーソル(クリックで敷設される点)の高さ線。
    DrawHeightLine(ctx, preview.startPos);
    if (preview.awaitingEndpoint) DrawHeightLine(ctx, preview.midPos);
    if (!preview.polyline.empty()) DrawHeightLine(ctx, preview.polyline.back());
    const RailTieHeights heights = ComputeRailTieHeights(editor.layingSettings.defaultTrackPlugins, ctx.selector);
    const float lift = heights.maxRailHeight + heights.maxTieHeight;
    ctx.app.DebugDrawList().AddCircle(preview.startPos + rs::core::Vec3(0, 1, 0) * lift, OverlayLineTuning::Embedded().dotRadiusPx,
                                      MakeTopStyle(colors.valid, 0.0f));
    const bool rejected = preview.endSnapUnreachable || (preview.haveSolve && !preview.solve.acceptable);
    if (preview.awaitingMidpoint || (preview.awaitingEndpoint && rejected)) {
        AddPolyline(ctx, preview.polyline, colors.invalidLine, MinLineWidthPx());
        return;
    }
    if (!preview.awaitingEndpoint || !preview.haveSolve || !preview.solve.acceptable) return;
    ctx.app.DebugDrawList().AddLine(preview.startPos, preview.midPos, MakeTopStyle(colors.invalidLineFaint, MinLineWidthPx()));
    if (!preview.polyline.empty())
        ctx.app.DebugDrawList().AddLine(preview.midPos, preview.polyline.back(), MakeTopStyle(colors.invalidLineFaint, MinLineWidthPx()));
    // 基準curveに対応するtrackが無い(偶数本)ときは、基準curveを描かない。
    if (!preview.referenceIsGuide)
        AddKindPolyline(ctx, preview.polyline, preview.polylineKind, &preview.polylineUp, lift, MinLineWidthPx(), 0);
    for (const rs::editor::PlacePreview::ArcChord& chord : preview.arcChords) {
        const uint32_t chordColor = chord.kind == rs::editor::PlacePreview::ArcChord::Kind::TransitionSpan
            ? colors.layingTransitionFill : colors.layingOtherFill;
        ctx.app.DebugDrawList().AddLine(chord.start, chord.end, MakeTopStyle(chordColor, MinLineWidthPx()));
        if (chord.showRadiusText) DrawRadiusText(ctx, chord.mid, chord.radius);
    }
    // 基準curve以外のtrack。区間の種類ごとの色分けは基準curveと同じで、透明度だけ下げる。
    for (size_t index = 0; index < preview.sidePolylines.size(); ++index) {
        static const std::vector<rs::world::AlignmentSegment::Kind> kNoKinds;
        const std::vector<rs::world::AlignmentSegment::Kind>& kinds =
            index < preview.sidePolylineKinds.size() ? preview.sidePolylineKinds[index] : kNoKinds;
        AddKindPolyline(ctx, preview.sidePolylines[index], kinds, nullptr, 0.0f, MinLineWidthPx(), kSidePolylineAlpha);
    }
}
} // namespace rs::app

#include "EditorViewport.h"

#include "../../core/DebugDraw.h"
#include "../gpu/App.h"
#include "OverlayColorTuning.h"
#include "OverlayLineTuning.h"
#include "ViewportOverlayGeometry.h"

namespace rs::app {
namespace {

// 番線1件のrailEdgesを指定色でなぞる。edgeが未解決(既に削除された等)なら黙って飛ばす。
void DrawPlatformEdges(const EditorViewportContext& ctx,
                       const rs::world::PlatformStopInfo& platform, uint32_t color,
                       float thickness) {
    rs::core::DebugStyle style;
    style.color = color;
    style.widthPx = thickness;
    for (const rs::world::Guid& edgeId : platform.railEdges) {
        const std::unordered_map<rs::world::Guid, rs::world::TrackEdge, rs::world::GuidHash>::const_iterator edgeIt =
            ctx.editor.graph.edges.find(edgeId);
        if (edgeIt == ctx.editor.graph.edges.end()) {
            continue;
        }
        const rs::world::TrackEdge& edge = edgeIt->second;
        std::vector<rs::core::Vec3> points;
        points.reserve(edge.spline.size());
        for (const rs::world::TrackNodeSample& sample : edge.spline) points.push_back(sample.pos);
        ctx.app.DebugDrawList().AddPolyline(points.data(), points.size(), false, style);
    }
}

// パネルでホバー中の行が対象にしている番線か。駅行のホバーはその駅の全番線が対象。
bool IsHovered(const rs::editor::Editor& editor, const rs::world::PlatformStopInfo& platform) {
    if (editor.hoveredPlatform != rs::world::NilGuid() && platform.id == editor.hoveredPlatform) {
        return true;
    }
    return editor.hoveredStation != rs::world::NilGuid() && platform.stationId == editor.hoveredStation;
}

}  // namespace

// 番線ダイヤ編集モードで、選択中/ホバー中の番線に紐づくrailEdgesをビューポート上に
// 強調表示するオーバーレイ。ホバーは駅・ダイヤパネルの行にマウスが乗っている間だけ立つ。
// 選択を後に描いて上へ重ねるので、同じ番線が両方に該当しても選択色が見える。
void DrawStationDiaOverlay(const EditorViewportContext& ctx) {
    if (!GetViewModeTraits(ctx.viewMode).stationDia) {
        return;
    }

    const OverlayColorTuning& colors = OverlayColorTuning::Embedded();

    const rs::world::PlatformStopInfo* selected = nullptr;
    for (const rs::world::PlatformStopInfo& platform : ctx.editor.graph.platforms) {
        if (platform.id == ctx.editor.selectedPlatform) {
            selected = &platform;
        }
        if (IsHovered(ctx.editor, platform)) {
            DrawPlatformEdges(ctx, platform, colors.hover, 4.0f);
        }
    }
    if (selected) {
        DrawPlatformEdges(ctx, *selected, colors.select, 5.0f);
    }
    // ポイント一覧でマウスが乗っている行のnodeを輪で示す。
    const std::unordered_map<rs::world::Guid, rs::world::TrackNode, rs::world::GuidHash>::const_iterator
        pointIt = ctx.editor.graph.nodes.find(ctx.editor.hoveredPointNode);
    if (pointIt != ctx.editor.graph.nodes.end()) {
        const OverlayLineTuning& lines = OverlayLineTuning::Embedded();
        ctx.app.DebugDrawList().AddCircle(ctx.editor.NodeRailTopPos(pointIt->second), lines.markerRadiusPx,
                                          MakeNodeLineStyle(colors.hover, lines.nodeLineWidthPx));
    }
}

}  // namespace rs::app

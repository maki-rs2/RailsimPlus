#include "ViewportOverlay.h"

#include "../../core/Epsilon.h"

#include <cmath>
#include <string>

#include "../../core/DebugDraw.h"
#include "../../editor/tool/Editor.h"
#include "../gpu/App.h"
#include "../plugin_session/LayoutSession.h"
#include "../scene/SceneRenderer.h"
#include "EdgePickInput.h"
#include "OverlayColorTuning.h"
#include "OverlayLineTuning.h"
#include "TrainPlacementHighlight.h"
#include "ViewportOverlayGeometry.h"
#include "ViewportPierBatchPlan.h"
#include "ViewportSectionOverlay.h"
#include "ViewportWireEditOverlay.h"

namespace rs::app {
namespace {


void UpdatePlacementHighlightSlots(const EditorViewportContext& ctx) {
    rs::editor::Editor& editor = ctx.editor;
    const bool visible = GetViewModeTraits(ctx.viewMode).placementHighlight;
    if (visible && editor.mode == rs::editor::ToolMode::Select) {
        const auto findCached = [&](const rs::world::Guid& edge, double s, const std::string& folder,
                                    rs::world::TrackPlacement& placement) -> bool {
            for (const TrackPluginPlacementItem& item : ctx.placementCache) {
                if (item.placement.position.edge == edge && item.folderUtf8 == folder &&
                    std::abs(item.placement.position.s - s) < rs::core::kPlacementPositionEpsM) {
                    placement = item.placement;
                    return true;
                }
            }
            return false;
        };
        const auto apply = [&](UINT slot, const std::string& folder,
                               const rs::world::TrackPlacement& placement, const float tint[4]) -> bool {
            const auto deactivate = [&]() {
                ctx.app.SetHighlightActive(slot, false, rs::core::Mat4::Identity(), tint);
                editor.highlightUploadedKey[slot].clear();
                return false;
            };
            if (folder.empty()) return deactivate();
            rs::core::Mat4 world = rs::world::ResolveWorldMatrix(placement, editor.graph);
            const TrackPluginEntry* entry = ctx.selector.FindTrackPluginEntry(folder);
            const bool isPier = entry != nullptr && entry->type == rs::legacy_pi::PluginType::Pier;
            // 橋脚は描画と同じ規則(高さ別メッシュ・低い橋脚は作らない・Y平行移動は地面)で出す。
            // 高さ別メッシュは高さごとに別物なので、アップロード済みの判定もバッチキーで行う。
            std::string uploadKey = folder;
            const rs::render::Mesh* mesh = nullptr;
            bool meshFound = false;
            if (isPier) {
                const PierBatchPlan plan = ComputePierBatchPlan(folder, entry, world);
                if (plan.skip) return deactivate();
                ZeroPierWorldY(world);
                uploadKey = plan.key;
                meshFound = ctx.sceneRenderer.TryGetPierAssemblyMesh(folder, plan.roundedHeight, ctx.log, &mesh);
            } else {
                meshFound = ctx.sceneRenderer.TryGetTrackPluginAssemblyMesh(folder, ctx.log, &mesh);
            }
            if (!meshFound || mesh == nullptr || mesh->vertices.empty()) return deactivate();
            if (editor.highlightUploadedKey[slot] != uploadKey) {
                ctx.app.UploadHighlightMesh(slot, *mesh);
                editor.highlightUploadedKey[slot] = uploadKey;
            }
            ctx.app.SetHighlightActive(slot, true, world, tint);
            return true;
        };
        const OverlayColorTuning& colors = OverlayColorTuning::Embedded();
        const OverlayTint hoverTintValue = MakeOverlayTint(colors.hover, colors.tintAlpha);
        const OverlayTint selectTintValue = MakeOverlayTint(colors.select, colors.tintAlpha);
        const OverlayTint dragTintValue = MakeOverlayTint(colors.drag, colors.tintAlpha);
        const float* hoverTint = hoverTintValue.rgba;
        const float* selectTint = selectTintValue.rgba;
        const float* dragTint = dragTintValue.rgba;
        if (editor.placementDragUi.active) {
            ctx.app.SetHighlightActive(0, false, rs::core::Mat4::Identity(), hoverTint);
            editor.highlightUploadedKey[0].clear();
            rs::world::TrackPlacement placement;
            placement.position.edge = editor.placementDragUi.edge;
            placement.position.s = editor.placementDragUi.previewS;
            placement.position.direction = editor.placementDragUi.direction;
            placement.lateralOffsetM = editor.placementDragUi.previewLateralOffsetM;
            placement.verticalOffsetM = editor.placementDragUi.verticalOffsetM;
            placement.keepUpright = editor.placementDragUi.keepUpright;
            apply(1, editor.placementDragUi.folder, placement, dragTint);
        } else {
            rs::world::TrackPlacement hover;
            const bool haveHover = editor.hoveredPlacementEdge != rs::world::NilGuid() &&
                findCached(editor.hoveredPlacementEdge, editor.hoveredPlacementS,
                           editor.hoveredPlacementFolder, hover);
            apply(0, haveHover ? editor.hoveredPlacementFolder : std::string(), hover, hoverTint);
            rs::world::TrackPlacement selected;
            const bool haveSelected = editor.HasSelectedPlacement() &&
                findCached(editor.selectedPlacementEdge, editor.selectedPlacementS,
                           editor.selectedPlacementFolder, selected);
            apply(1, haveSelected ? editor.selectedPlacementFolder : std::string(), selected, selectTint);
        }
    } else if (visible) {
        ctx.app.SetHighlightActive(0, false, rs::core::Mat4::Identity(), nullptr);
        ctx.app.SetHighlightActive(1, false, rs::core::Mat4::Identity(), nullptr);
        editor.highlightUploadedKey[0].clear();
        editor.highlightUploadedKey[1].clear();
    }
}

void DrawTrackEdges(const EditorViewportContext& ctx) {
    const OverlayLineTuning& tuning = OverlayLineTuning::Embedded();
    const OverlayColorTuning& colors = OverlayColorTuning::Embedded();
    const auto classifyPointEdge = [&](const rs::world::Guid& edgeGuid,
                                       const rs::world::TrackEdge& edge) -> int {
        if (ctx.editor.selectedNode == rs::world::NilGuid()) return 0;
        const std::unordered_map<rs::world::Guid, rs::world::TrackNode,
                                 rs::world::GuidHash>::const_iterator nodeIt =
            ctx.editor.graph.nodes.find(ctx.editor.selectedNode);
        if (nodeIt == ctx.editor.graph.nodes.end()) return 0;
        const rs::world::TrackNode& node = nodeIt->second;
        if (!node.IsPoint() ||
            (edge.nodeA != ctx.editor.selectedNode && edge.nodeB != ctx.editor.selectedNode)) return 0;
        for (int index = 0; index < static_cast<int>(node.links.size()); ++index) {
            if (node.links[static_cast<size_t>(index)].edge != edgeGuid) continue;
            if (!node.IsBranching(node.links[static_cast<size_t>(index)].side)) return 1;
            return node.IsLinkActive(index) ? 2 : 0;
        }
        return 0;
    };
    for (const std::pair<const rs::world::Guid, rs::world::TrackEdge>& entry : ctx.editor.graph.edges) {
        const rs::world::TrackEdge& edge = entry.second;
        if (rs::world::IsCenterlineOnlyEdge(edge)) continue;
        const bool selected = ctx.editor.IsEdgeSelected(entry.first);
        const bool hovered = entry.first == ctx.editor.hoveredEdge;
        uint32_t color = colors.edgeIdle;
        if (selected) color = colors.edgeSelected;
        else if (hovered) color = colors.edgeHovered;
        else {
            const int pointRole = classifyPointEdge(entry.first, edge);
            if (pointRole == 1) color = colors.edgeRoleStart;
            else if (pointRole == 2) color = colors.edgeRoleEnd;
        }
        std::vector<rs::core::Vec3> points;
        points.reserve(edge.spline.size());
        for (const rs::world::TrackNodeSample& sample : edge.spline)
            points.push_back(sample.pos + sample.up * edge.railTopLift);
        ctx.app.DebugDrawList().AddPolyline(points.data(), points.size(), false,
                                            MakeTopStyle(color, selected ? tuning.selectedWidthPx : tuning.minWidthPx));
    }
}

void DrawCenterlines(const EditorViewportContext& ctx) {
    const OverlayLineTuning& tuning = OverlayLineTuning::Embedded();
    const OverlayColorTuning& colors = OverlayColorTuning::Embedded();
    for (const std::pair<const rs::world::Guid, rs::world::TrackEdge>& entry : ctx.editor.graph.edges) {
        const rs::world::TrackEdge& edge = entry.second;
        if (!edge.centerlineOnly) continue;
        const bool selected = ctx.editor.IsEdgeSelected(entry.first);
        const bool hovered = entry.first == ctx.editor.hoveredEdge;
        const uint32_t color = selected ? colors.edgeSelected :
            hovered ? colors.edgeHovered : colors.edgeMarkerIdle;
        std::vector<rs::core::Vec3> points;
        points.reserve(edge.spline.size());
        for (const rs::world::TrackNodeSample& sample : edge.spline) points.push_back(sample.pos);
        ctx.app.DebugDrawList().AddPolyline(points.data(), points.size(), false,
                                            MakeTopStyle(color, selected ? tuning.selectedWidthPx : tuning.minWidthPx));
    }
}
} // namespace

void DrawEditorOverlay(const EditorViewportContext& ctx, const ViewProjResult& viewProj,
                       float mouseX, float mouseY) {
    UpdatePlacementHighlightSlots(ctx);
    DrawBoxSelect(ctx.editor);
    if (!GetViewModeTraits(ctx.viewMode).trackOverlay) {
        ctx.lastArcPreview = rs::editor::PlacePreview{};
        return;
    }
    DrawTrackEdges(ctx);
    DrawTrainPlacementHighlight(ctx);
    DrawCenterlines(ctx);
    DrawTrackNodeGeometry(ctx);
    if (GetViewModeTraits(ctx.viewMode).trackEditing) DrawWireEditOverlay(ctx);
    if (!GetViewModeTraits(ctx.viewMode).arcOverlay) {
        ctx.lastArcPreview = rs::editor::PlacePreview{};
        return;
    }
    DrawArcToolPreviewGeometry(ctx, viewProj, mouseX, mouseY);
    DrawSectionOverlay(ctx, viewProj);
}
} // namespace rs::app

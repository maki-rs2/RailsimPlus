#include "ViewportOverlay.h"

#include <string>

#include "../../core/DebugDraw.h"
#include "../../editor/tool/Editor.h"
#include "../entry/SlowSectionLog.h"
#include "../gpu/App.h"
#include "../scene/ScenePicking.h"
#include "../scene/SceneRenderer.h"
#include "OverlayColorTuning.h"
#include "OverlayLineTuning.h"
#include "SceneryTransformGizmo.h"

namespace rs::app {
namespace {
rs::core::DebugStyle MakeMarkerStyle(uint32_t color) {
    rs::core::DebugStyle style;
    style.color = color;
    style.widthPx = OverlayLineTuning::Embedded().minWidthPx;
    style.depthMode = rs::core::DebugDepthMode::AlwaysOnTop;
    return style;
}
} // namespace

void DrawSceneEntityOverlay(const EditorViewportContext& ctx, const ViewProjResult& viewProj) {
    rs::editor::Editor& editor = ctx.editor;
    const bool active = GetViewModeTraits(ctx.viewMode).sceneEntityOverlay;
    const rs::world::Guid hoverId = active ? editor.hoveredSceneEntityId : rs::world::NilGuid();
    const rs::world::Guid selectedId = active ? editor.selectedSceneEntityId : rs::world::NilGuid();
    const bool placementActive = GetViewModeTraits(ctx.viewMode).placementHighlight &&
        (editor.hoveredPlacementEdge != rs::world::NilGuid() || editor.HasSelectedPlacement() ||
         editor.placementDragUi.active);
    if (placementActive) return;
    if (rs::world::GuidIsNil(hoverId) && rs::world::GuidIsNil(selectedId)) {
        const float noTint[4] = {0, 0, 0, 0};
        ctx.app.SetHighlightActive(0, false, rs::core::Mat4::Identity(), noTint);
        ctx.app.SetHighlightActive(1, false, rs::core::Mat4::Identity(), noTint);
        ctx.app.SetSlotHighlight(0, false, 0, noTint);
        ctx.app.SetSlotHighlight(1, false, 0, noTint);
        return;
    }
    const auto findCandidate = [&](const rs::world::Guid& id, SceneRenderer::ScenePickItem& item) -> bool {
        if (rs::world::GuidIsNil(id)) return false;
        if (ctx.sceneRenderer.TryGetSceneryPickCandidate(ctx.sceneRegistry, editor.graph, id, ctx.log, item)) return true;
        return ctx.sceneRenderer.TryGetTrainPickCandidate(ctx.sceneRegistry, id, item);
    };
    const auto applyHighlight = [&](UINT slot, const rs::world::Guid& id, const float tint[4]) -> bool {
        SceneRenderer::ScenePickItem item;
        if (!findCandidate(id, item)) {
            ctx.app.SetHighlightActive(slot, false, rs::core::Mat4::Identity(), tint);
            ctx.app.SetSlotHighlight(slot, false, 0, tint);
            editor.highlightUploadedKey[slot].clear();
            return false;
        }
        if (!item.isStaticInstanced) {
            ctx.app.SetHighlightActive(slot, false, rs::core::Mat4::Identity(), tint);
            editor.highlightUploadedKey[slot].clear();
            UINT highlightSlot = 0;
            if (!ctx.sceneRenderer.FindHighlightSlotForEntity(id, highlightSlot)) {
                ctx.app.SetSlotHighlight(slot, false, 0, tint);
                return false;
            }
            ctx.app.SetSlotHighlight(slot, true, highlightSlot, tint);
            return true;
        }
        ctx.app.SetSlotHighlight(slot, false, 0, tint);
        const std::string key =
            SceneRenderer::MakeStaticInstanceKey(item.folderUtf8, item.switchOptions, item.signalModel, item.definitionFile);
        if (editor.highlightUploadedKey[slot] != key) {
            const rs::render::Mesh* mesh = nullptr;
            if (!ctx.sceneRenderer.TryGetStaticInstanceMesh(item.folderUtf8, item.switchOptions, ctx.log, &mesh,
                                                            item.signalModel, item.definitionFile) ||
                mesh == nullptr || mesh->Empty()) {
                ctx.app.SetHighlightActive(slot, false, rs::core::Mat4::Identity(), tint);
                editor.highlightUploadedKey[slot].clear();
                return false;
            }
            SlowSectionLog slow("HighlightUpload(配置物の輪郭の切り替え)", 30.0);
            ctx.app.UploadHighlightMesh(slot, *mesh);
            editor.highlightUploadedKey[slot] = key;
        }
        ctx.app.SetHighlightActive(slot, true, item.candidate.worldMatrix, tint);
        return true;
    };
    const OverlayColorTuning& colors = OverlayColorTuning::Embedded();
    const OverlayTint hoverTint = MakeOverlayTint(colors.hover, colors.tintAlpha);
    const OverlayTint selectedTint = MakeOverlayTint(colors.select, colors.tintAlpha);
    const bool hoverTinted = applyHighlight(0, hoverId, hoverTint.rgba);
    const bool selectedTinted = applyHighlight(1, selectedId, selectedTint.rgba);
    const auto drawMarker = [&](const rs::world::Guid& id, bool tinted, uint32_t color) {
        if (rs::world::GuidIsNil(id) || tinted) return;
        SceneRenderer::ScenePickItem item;
        if (!findCandidate(id, item)) return;
        const rs::core::Mat4& world = item.candidate.worldMatrix;
        const rs::core::Vec3 center(world.m[12], world.m[13], world.m[14]);
        if (item.candidate.hasAabb)
            ctx.app.DebugDrawList().AddRect(center, OverlayLineTuning::Embedded().markerRadiusPx, OverlayLineTuning::Embedded().markerRadiusPx, MakeMarkerStyle(color));
        else
            ctx.app.DebugDrawList().AddCircle(center, OverlayLineTuning::Embedded().markerRadiusPx, MakeMarkerStyle(color));
    };
    drawMarker(hoverId, hoverTinted, colors.hover);
    for (const rs::world::Guid& id : editor.SelectedSceneEntities()) {
        if (id != selectedId) drawMarker(id, false, colors.select);
    }
    drawMarker(selectedId, selectedTinted, colors.select);
    DrawSceneryTransformGizmo(ctx, viewProj);
}
} // namespace rs::app

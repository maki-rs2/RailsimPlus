#include "ViewportInput.h"

#include <algorithm>
#include <vector>

#include "SceneryTransformGizmo.h"
#include "ViewportInputInternal.h"
#include "../scene/ScenePicking.h"
#include "../scene/SceneRenderer.h"
#include "../../core/Math.h"
#include "../../editor/tool/Editor.h"
#include "../../world/scene/SceneRegistry.h"

namespace rs::app {

void HandleSceneEntitySelectionInput(const EditorViewportContext& ctx, const rs::app::ViewProjResult& vpr,
                                     rs::app::input::PointerPos mouse, bool sceneEntitySelectionEnabled) {
    rs::editor::Editor& editor = ctx.editor;
    const rs::app::input::InputSource& input = ctx.input;
    if (editor.sceneEntityGizmoDrag.active) {
        HandleSceneryTransformGizmo(ctx, vpr, mouse);
        return;
    }
    editor.sceneEntityGizmoHover = {};
    if (!sceneEntitySelectionEnabled) {
        editor.hoveredSceneEntityId = rs::world::NilGuid();
        editor.hoveredPlacementEdge = rs::world::NilGuid();
        editor.hoveredPlacementFolder.clear();
        return;
    }
    if (!input.AllowsPointer(rs::app::input::InputContext::Edit)) return;
    if (!IsMouseInViewport(mouse, ctx.width, ctx.height)) {
        editor.hoveredSceneEntityId = rs::world::NilGuid();
        editor.hoveredPlacementEdge = rs::world::NilGuid();
        editor.hoveredPlacementFolder.clear();
        return;
    }
    if (HandleSceneryTransformGizmo(ctx, vpr, mouse)) return;

    const bool shift = input.MatchesPointer(rs::app::input::PointerAction::SelectionExtend);
    const bool leftClicked = input.ButtonClicked(input.ButtonFor(rs::app::input::PointerAction::Select));
    rs::world::Guid hoverEntityId = rs::world::NilGuid();
    rs::core::Ray ray;
    if (!rs::core::ScreenToRay(mouse.x, mouse.y, vpr.viewProj, ctx.width, ctx.height, ray)) return;
    const bool hoverScenery = PickSceneEntity(ctx.sceneRenderer, ctx.sceneRegistry, editor.graph, ctx.log, ray,
        mouse.x, mouse.y, vpr.viewProj, ctx.width, ctx.height, 10.0f, hoverEntityId);
    editor.hoveredSceneEntityId = hoverScenery ? hoverEntityId : rs::world::NilGuid();

    rs::editor::BoxSelectUiState& box = editor.boxSelectUi;
    if (leftClicked && !hoverScenery) {
        box.Clear();
        box.armed = true;
        box.startX = box.endX = mouse.x;
        box.startY = box.endY = mouse.y;
    } else if (leftClicked && hoverScenery) {
        if (shift) editor.ToggleSceneEntitySelection(hoverEntityId);
        else editor.SelectSceneEntity(hoverEntityId);
        editor.ClearSelectedPlacement();
    }
    if (box.armed && input.ButtonDragging(input.ButtonFor(rs::app::input::PointerAction::Select), 4.0f)) {
        box.active = true;
        box.endX = mouse.x;
        box.endY = mouse.y;
    }
    if (!box.armed || !input.ButtonReleased(input.ButtonFor(rs::app::input::PointerAction::Select))) return;
    if (box.active) {
        std::vector<SceneRenderer::ScenePickItem> items;
        ctx.sceneRenderer.CollectSceneryPickCandidates(ctx.sceneRegistry, editor.graph, ctx.log, items);
        std::vector<rs::world::Guid> hits;
        const float x0 = std::min(box.startX, mouse.x), x1 = std::max(box.startX, mouse.x);
        const float y0 = std::min(box.startY, mouse.y), y1 = std::max(box.startY, mouse.y);
        for (const SceneRenderer::ScenePickItem& item : items) {
            if (!item.candidate.hasAabb) continue;
            float minX = 0.0f, minY = 0.0f, maxX = 0.0f, maxY = 0.0f;
            if (ProjectAabbScreenRect(item.candidate.worldMatrix, item.candidate.aabbMin, item.candidate.aabbMax,
                                      vpr.viewProj, ctx.width, ctx.height, minX, minY, maxX, maxY) &&
                maxX >= x0 && minX <= x1 && maxY >= y0 && minY <= y1) hits.push_back(item.entityId);
        }
        if (shift) {
            for (const rs::world::Guid& id : hits) {
                if (!editor.IsSceneEntitySelected(id)) editor.ToggleSceneEntitySelection(id);
            }
        } else {
            editor.SelectSceneEntities(hits);
        }
    } else if (!shift) {
        editor.ClearSelectedSceneEntity();
    }
    box.Clear();
}

} // namespace rs::app

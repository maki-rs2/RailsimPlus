#include "EdgePickInput.h"

#include "OverlayLineTuning.h"

#include "imgui.h"

#include "../../editor/tool/Editor.h"

namespace rs::app {

namespace {

// 四角の塗りと枠の色(ABGR)。
constexpr ImU32 kBoxFillColor = IM_COL32(90, 160, 255, 40);
constexpr ImU32 kBoxEdgeColor = IM_COL32(90, 160, 255, 200);

} // namespace

EdgePickEvent UpdateEdgePick(const EditorViewportContext& ctx, const rs::app::ViewProjResult& vpr,
                             rs::app::input::PointerPos mouse, const EdgePickInput& pick) {
    using rs::app::input::PointerButton;
    EdgePickEvent event;
    rs::editor::Editor& editor = ctx.editor;
    rs::editor::BoxSelectUiState& box = editor.boxSelectUi;
    const bool shift = ctx.input.MatchesPointer(rs::app::input::PointerAction::SelectionExtend);
    const bool hasHovered = pick.hoveredEdge != rs::world::NilGuid();

    bool pressedOnEmpty = pick.pressedOnEmpty;
    if (pick.reportPlainClick) {
        const bool pressed = ctx.input.ButtonClicked(ctx.input.ButtonFor(rs::app::input::PointerAction::Select)) && !shift;
        pressedOnEmpty = pressed && !hasHovered;
        if (pressed && hasHovered) {
            event.kind = EdgePickEvent::Kind::Click;
            event.edges.push_back(pick.hoveredEdge);
        }
    }
    if (shift && ctx.input.ButtonTapped(ctx.input.ButtonFor(rs::app::input::PointerAction::Select)) && hasHovered) {
        event.kind = EdgePickEvent::Kind::ShiftTap;
        event.edges.push_back(pick.hoveredEdge);
    }

    if (pressedOnEmpty) {
        box.Clear();
        box.armed = true;
        box.startX = box.endX = mouse.x;
        box.startY = box.endY = mouse.y;
    }
    if (!box.armed) return event;

    if (ctx.input.ButtonReleased(ctx.input.ButtonFor(rs::app::input::PointerAction::Select))) {
        if (box.active) {
            event.kind = EdgePickEvent::Kind::Box;
            event.edges = editor.PickEdgesInRect(box.startX, box.startY, mouse.x, mouse.y, vpr.viewProj,
                                                 ctx.width, ctx.height);
        }
        box.Clear();
        return event;
    }
    if (ctx.input.ButtonDragging(ctx.input.ButtonFor(rs::app::input::PointerAction::Select), OverlayLineTuning::Embedded().dragStartDistancePx)) {
        box.active = true;
        box.endX = mouse.x;
        box.endY = mouse.y;
    }
    return event;
}

void CancelEdgePickIfReleased(const EditorViewportContext& ctx) {
    if (ctx.input.ButtonReleased(ctx.input.ButtonFor(rs::app::input::PointerAction::Select))) ctx.editor.boxSelectUi.Clear();
}

void DrawBoxSelect(const rs::editor::Editor& editor) {
    const rs::editor::BoxSelectUiState& box = editor.boxSelectUi;
    if (!box.active) return;
    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    const ImVec2 a(box.startX, box.startY);
    const ImVec2 b(box.endX, box.endY);
    drawList->AddRectFilled(a, b, kBoxFillColor);
    drawList->AddRect(a, b, kBoxEdgeColor);
}

} // namespace rs::app

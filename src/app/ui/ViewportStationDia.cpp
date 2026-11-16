#include "EditorViewport.h"

#include <memory>

#include "../../editor/tool/PlatformCommand.h"
#include "EdgePickInput.h"
#include "ViewportInput.h"

namespace rs::app {

// 番線ダイヤ編集モードでのビューポートの入力を処理する。
// レールを選ぶ共通の操作(UpdateEdgePick: クリック・Shift+クリック・矩形)で、選択中の番線の
// railEdgesへレールを追加・除去する。変更はUndo可能なSetPlatformPropertyCommand経由で行う。
void HandleStationDiaInput(const EditorViewportContext& ctx, const rs::app::ViewProjResult& vpr,
                           rs::app::input::PointerPos mouse, bool enabled) {
    if (!enabled) return;
    if (!ctx.input.AllowsPointer(rs::app::input::InputContext::Edit) || !IsMouseInViewport(mouse, ctx.width, ctx.height)) {
        CancelEdgePickIfReleased(ctx);
        return;
    }
    rs::editor::Editor& editor = ctx.editor;
    rs::world::PlatformStopInfo* platform = nullptr;
    for (rs::world::PlatformStopInfo& item : editor.graph.platforms) {
        if (item.id == editor.selectedPlatform) {
            platform = &item;
            break;
        }
    }
    if (!platform) {
        CancelEdgePickIfReleased(ctx);
        return;
    }
    rs::core::Ray ray;
    if (!rs::core::ScreenToRay(mouse.x, mouse.y, vpr.viewProj, ctx.width, ctx.height, ray)) return;
    EdgePickInput pick;
    pick.reportPlainClick = true;
    pick.hoveredEdge = editor.PickEdge(ray, mouse.x, mouse.y, vpr.viewProj, ctx.width, ctx.height);
    const EdgePickEvent event = UpdateEdgePick(ctx, vpr, mouse, pick);
    if (!event.Any()) return;
    rs::editor::PlatformPropertyDelta delta;
    delta.railEdges = ApplyEdgePickToList(platform->railEdges, event);
    if (*delta.railEdges == platform->railEdges) return;
    editor.ExecutePlatformCommand(std::make_unique<rs::editor::SetPlatformPropertyCommand>(platform->id, std::move(delta)));
}

}  // namespace rs::app

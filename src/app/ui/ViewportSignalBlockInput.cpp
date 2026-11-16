#include "EditorViewport.h"

#include <memory>

#include "../../editor/tool/BlockCommand.h"
#include "../../world/alignment/Alignment.h"
#include "../../world/alignment/SpeedSignalRegistry.h"
#include "../action/SceneSignalAspectSource.h"
#include "../action/SceneSignalSidecarSource.h"
#include "../action/SignalBlockAction.h"
#include "../action/ToolCancelAction.h"
#include "EdgePickInput.h"
#include "ViewportInput.h"

namespace rs::app {

// 信号・閉塞編集モードでのビューポートの入力を処理する。
// 信号の配置待ち(editor.signalBlockArmedAction、パネルの「信号を置く」でセットされる)の間は、
// レールの左クリックで位置を決め、担当閉塞が複数なら候補から選ぶ。それ以外は、レールを選ぶ共通の操作(UpdateEdgePick: クリック・
// Shift+クリック・矩形)を受け、閉塞を選んでいれば閉塞のレールの追加・除去に、選んでいなければ
// エディタのレール選択(パネルの「閉塞にする」の元)に反映する。
void HandleSignalBlockInput(const EditorViewportContext& ctx, const rs::app::ViewProjResult& vpr,
                            rs::app::input::PointerPos mouse, bool enabled) {
    if (ctx.signalBlockLink != nullptr) ctx.signalBlockLink->hoveredRail.reset();
    if (!enabled) return;

    rs::editor::Editor& editor = ctx.editor;
    SceneSignalSidecarSource sidecarSource(ctx.sceneRenderer);
    SceneSignalAspectSource aspectSource(ctx.sceneRenderer);
    SignalBlockAction action(editor, sidecarSource, aspectSource);
    const bool cancelled = !ctx.escapeHandledByWorkspace &&
        ctx.input.Triggered(rs::app::input::InputAction::EditorCancel);
    if (cancelled) {
        ToolCancelAction(editor, ctx.viewMode).CancelOperation();
    }
    const bool clicked = ctx.input.ButtonClicked(ctx.input.ButtonFor(rs::app::input::PointerAction::Select));
    if (editor.speedSignalPending && !clicked) {
        action.UpdateSpeedSignalHover(
            rs::world::NilGuid(), 0.0, false, &ctx.layoutSession.warpLinks, ctx.layoutSession.activeIndex);
    }
    if (!ctx.input.AllowsPointer(rs::app::input::InputContext::Edit) || !IsMouseInViewport(mouse, ctx.width, ctx.height)) {
        if (cancelled) return;
        if (!editor.speedSignalPending) editor.speedSignalHover = {};
        CancelEdgePickIfReleased(ctx);
        return;
    }
    rs::core::Ray ray;
    if (!rs::core::ScreenToRay(mouse.x, mouse.y, vpr.viewProj, ctx.width, ctx.height, ray)) return;

    double s = 0.0;
    const rs::world::Guid edge = editor.PickEdgeS(ray, mouse.x, mouse.y, vpr.viewProj, ctx.width,
                                                 ctx.height, s,
                                                 rs::editor::Editor::kDefaultEdgePickRadiusPx, true);
    if (ctx.signalBlockLink != nullptr && !rs::world::GuidIsNil(edge)) {
        ctx.signalBlockLink->hoveredRail = workspace::RailLabelAnchor{edge, s};
    }
    if (cancelled) return;

    if (editor.signalBlockArmedAction == rs::editor::SignalBlockArmedAction::PlaceSpeedSignal) {
        // ホバー結果(置けるか)は毎フレームここで1回だけ更新し、マーカー描画とパネルは読むだけにする。
        // 複線中心線(実体の走行面を持たない)は対象にしない。
        const bool reverse = ctx.input.MatchesPointer(rs::app::input::PointerAction::PlacementReverse);
        if (clicked) {
            action.BeginSpeedSignalPlacement(edge, s, reverse, &ctx.layoutSession.warpLinks,
                ctx.layoutSession.activeIndex);
        } else if (!editor.speedSignalPending) {
            action.UpdateSpeedSignalHover(edge, s,
                reverse, &ctx.layoutSession.warpLinks, ctx.layoutSession.activeIndex);
        }
        return;
    }

    EdgePickInput pick;
    pick.reportPlainClick = true;
    pick.hoveredEdge = edge;
    const EdgePickEvent event = UpdateEdgePick(ctx, vpr, mouse, pick);
    if (!event.Any()) return;

    const rs::world::Block* block = rs::world::FindBlock(editor.graph, editor.selectedBlock);
    if (block == nullptr) {
        ApplyEdgePickToSelection(editor, event);
        return;
    }
    // 範囲の粒度はレール1本単位。外したレール上の担当の信号は、SetBlockPropertyCommandが一緒に消す
    // (1回のUndoで戻る)。
    rs::editor::BlockPropertyDelta delta;
    delta.edges = ApplyEdgePickToList(block->edges, event);
    if (*delta.edges == block->edges) return;
    editor.ExecutePlatformCommand(
        std::make_unique<rs::editor::SetBlockPropertyCommand>(block->guid, std::move(delta)));
}

} // namespace rs::app

#include "NodeDragInput.h"

#include "../../core/Epsilon.h"

#include "OverlayLineTuning.h"

#include "../../core/Math.h"
#include "../../editor/tool/Editor.h"
#include "../../world/geometry/GeometrySolvers.h"

namespace rs::app {

void LatchSelectPress(rs::editor::Editor& editor, const rs::app::input::InputSource& input,
                      bool leftClickForEditor) {
    if (input.ButtonClicked(input.ButtonFor(rs::app::input::PointerAction::Select))) editor.selectPress.Clear();
    if (!leftClickForEditor) return;

    rs::editor::SelectPressUiState& press = editor.selectPress;
    // ハンドルはノード本体より優先する(ホバーと同じ順)。選択中のノードのハンドルだけが対象になる。
    if (editor.hoveredTangentHandle && editor.selectedNode != rs::world::NilGuid()) {
        press.target = rs::editor::PressTarget::TangentHandle;
        press.node = editor.selectedNode;
        press.side = (editor.hoveredTangentHandleSide < 0) ? -1 : +1;
    } else if (editor.hoveredNode != rs::world::NilGuid()) {
        press.target = rs::editor::PressTarget::Node;
        press.node = editor.hoveredNode;
    }
}

bool UpdateTangentDrag(const EditorViewportContext& ctx, const rs::app::ViewProjResult& vpr,
                       rs::app::input::PointerPos mouse) {
    rs::editor::Editor& editor = ctx.editor;
    const rs::app::input::InputSource& input = ctx.input;

    if (editor.tangentDragUi.active) {
        rs::core::Ray ray;
        if (rs::core::ScreenToRay(mouse.x, mouse.y, vpr.viewProj, ctx.width, ctx.height, ray)) {
            rs::core::Vec3 hit;
            if (rs::core::RayHitPlaneY(ray, editor.tangentDragUi.planeY, hit)) {
                const auto nit = editor.graph.nodes.find(editor.tangentDragUi.guid);
                if (nit != editor.graph.nodes.end()) {
                    rs::core::Vec3 toHit = hit - editor.NodeRailTopPos(nit->second);
                    toHit.y = 0.0f;
                    const float len = toHit.Length();
                    if (len > rs::core::kDragTangentLengthEpsM) {
                        const rs::core::Vec3 xzUnit = toHit * (1.0f / len);
                        // 垂直(Y)成分はドラッグ開始時点の元の接線のY成分を引き継ぎ、XZ成分は
                        // side==-1(次数2ノードの逆方向ハンドル)なら符号反転する
                        // (rs::world::ComposeTangentDragCandidate)。
                        const rs::core::Vec3 combined = rs::world::ComposeTangentDragCandidate(
                            xzUnit, editor.tangentDragUi.originalTangentY, editor.tangentDragUi.side);
                        const float combinedLen = combined.Length();
                        if (combinedLen > rs::core::kDegenerateVectorEps)
                            editor.tangentDragUi.previewTangent = combined * (1.0f / combinedLen);
                    }
                }
            }
        }
        const rs::editor::NodeDragPlan curvePreview =
            editor.PreviewSetNodeTangent(editor.tangentDragUi.guid, editor.tangentDragUi.previewTangent);
        editor.tangentDragUi.preview.ok = curvePreview.ok;
        editor.tangentDragUi.preview.edges = curvePreview.edges;
        if (input.ButtonReleased(input.ButtonFor(rs::app::input::PointerAction::Select))) {
            if (curvePreview.ok)
                editor.SetNodeTangent(editor.tangentDragUi.guid, editor.tangentDragUi.previewTangent);
            editor.CancelTangentDrag();
        }
        return true;
    }

    // 開始は、押した瞬間に掴んだ記録で決める(今のホバーや選択は見ない)。
    const rs::editor::SelectPressUiState& press = editor.selectPress;
    if (press.target != rs::editor::PressTarget::TangentHandle || input.MatchesPointer(rs::app::input::PointerAction::CameraHeight) ||
        !input.ButtonDragging(input.ButtonFor(rs::app::input::PointerAction::Select), OverlayLineTuning::Embedded().dragStartDistancePx)) {
        return false;
    }
    const auto nit = editor.graph.nodes.find(press.node);
    if (nit == editor.graph.nodes.end()) return false;
    editor.tangentDragUi.active = true;
    editor.tangentDragUi.guid = press.node;
    editor.tangentDragUi.side = press.side;
    rs::core::Vec3 t = nit->second.tangent;
    const float len = t.Length();
    editor.tangentDragUi.previewTangent = (len > rs::core::kDegenerateVectorEps) ? t * (1.0f / len) : rs::core::Vec3(1, 0, 0);
    editor.tangentDragUi.originalTangentY = nit->second.tangent.y;
    //レール頭の高さの平面で追従(ハンドルの表示高さと同じ)
    editor.tangentDragUi.planeY = editor.NodeRailTopPos(nit->second).y;
    return true;
}

bool UpdateNodeDrag(const EditorViewportContext& ctx, const rs::app::ViewProjResult& vpr,
                    rs::app::input::PointerPos mouse) {
    rs::editor::Editor& editor = ctx.editor;
    const rs::app::input::InputSource& input = ctx.input;
    bool consumed = false;

    if (editor.nodeDragUi.active) {
        consumed = true;
        rs::core::Ray ray;
        if (rs::core::ScreenToRay(mouse.x, mouse.y, vpr.viewProj, ctx.width, ctx.height, ray)) {
            rs::core::Vec3 hit;
            // カーソルが指す水平面は、ドラッグ中の高さ変更(↑↓キー/Ctrl+ホイール)ぶんだけ上げる。
            // 敷設ツールの敷設高さと同じ扱いで、ノードとカーソルがずれない。
            // 平面はレール頭の高さ、保存するnode位置はレール頭ぶん下げる。
            const float planeY = editor.nodeDragUi.planeY + editor.nodeDragUi.railLift + editor.nodeDragUi.heightOffset;
            if (rs::core::RayHitPlaneY(ray, planeY, hit)) {
                hit.y -= editor.nodeDragUi.railLift;
                editor.ClipToSurface(hit);
                editor.nodeDragUi.previewPos = hit;
            }
        }
        const rs::editor::NodeDragPlan curvePreview =
            editor.PreviewMoveNode(editor.nodeDragUi.guid, editor.nodeDragUi.previewPos);
        editor.nodeDragUi.preview.ok = curvePreview.ok;
        editor.nodeDragUi.preview.edges = curvePreview.edges;
        if (input.ButtonReleased(input.ButtonFor(rs::app::input::PointerAction::Select))) {
            if (curvePreview.ok) editor.MoveNodeTo(editor.nodeDragUi.guid, editor.nodeDragUi.previewPos);
            editor.CancelNodeDrag();
        }
    }

    // 開始は、押した瞬間に掴んだ記録で決める(今のホバーや選択は見ない)。
    const rs::editor::SelectPressUiState& press = editor.selectPress;
    if (editor.nodeDragUi.active || press.target != rs::editor::PressTarget::Node ||
        input.MatchesPointer(rs::app::input::PointerAction::CameraHeight) || !input.ButtonDragging(input.ButtonFor(rs::app::input::PointerAction::Select), OverlayLineTuning::Embedded().dragStartDistancePx)) {
        return consumed;
    }
    const auto nit = editor.graph.nodes.find(press.node);
    if (nit == editor.graph.nodes.end()) return consumed;
    if (!editor.CanMoveNode(press.node) && !editor.CanMoveCenterlineNode(press.node)) return consumed;
    editor.nodeDragUi.active = true;
    editor.nodeDragUi.guid = press.node;
    editor.nodeDragUi.previewPos = nit->second.pos;
    editor.nodeDragUi.planeY = nit->second.pos.y;
    editor.nodeDragUi.railLift = editor.NodeRailTopPos(nit->second).y - nit->second.pos.y;
    editor.nodeDragUi.preview.Clear();
    return true;
}

} // namespace rs::app

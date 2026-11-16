#include "ViewportInputInternal.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <string>
#include <vector>

#include "../../core/Math.h"
#include "../../world/geometry/GeometrySolvers.h"
#include "../../editor/tool/Editor.h"
#include "../../editor/tool/SceneEntityCommand.h"
#include "../../editor/tool/EditorSectionEdit.h"
#include "../../plugins/load/PluginDefinitionLookup.h"
#include "../../world/alignment/TrackTraversal.h"
#include "../../world/alignment/TrackQuery.h"
#include "../../world/scene/ScenePlacement.h"
#include "../scene/TrackPluginPlacement.h"
#include "../scene/PlacementMaterialize.h"
#include "EdgePickInput.h"
#include "NodeDragInput.h"
#include "PlacementDragInput.h"
#include "ViewportSectionOverlay.h"

namespace rs::app {

namespace {

// folderUtf8の複線一括本数(RailSim II の TrackNum、TrackPluginPlacement.hのMultiTrackNumと
// 同じ規約)。手動追加時にtrackNum>1なら中心線edgeへretargetするか判定するために
// 使う(ResolveCenterlineEdgeForと組み合わせ)。
int ResolveFolderTrackNum(const std::string& folderUtf8,
                         const std::vector<rs::app::TrackPluginEntry>& entries) {
    for (const rs::app::TrackPluginEntry& e : entries) {
        if (e.folderUtf8 == folderUtf8) return rs::app::MultiTrackNum(e.def, e.type);
    }
    return 1;
}

} // namespace

void HandlePlaceArcModeInput(const EditorViewportContext& ctx, const rs::app::ViewProjResult& vpr,
                             rs::app::input::PointerPos mouse, bool leftClickForEditor) {
    rs::editor::Editor& gEditor = ctx.editor;
    const UINT gCurWidth = ctx.width;
    const UINT gCurHeight = ctx.height;
    if (leftClickForEditor) {
        // edge上からの分岐開始・ノードスナップ・地面クリックは、すべてOnClickScreenが決める。
        gEditor.OnClickScreen(mouse.x, mouse.y, vpr.viewProj, gCurWidth, gCurHeight);
    }
    // 取り消しは、右ボタンを短く押して離したときだけ。長押し・ドラッグ(視点操作)では解除しない。
    if (ctx.input.ButtonTapped(ctx.input.ButtonFor(rs::app::input::PointerAction::Cancel))) {
        gEditor.CancelPlacement();
    }
}

bool HandleSectionSplitModeInput(const EditorViewportContext& ctx,
                                 const rs::app::ViewProjResult& vpr, rs::app::input::PointerPos mouse,
                                 bool leftClickForEditor) {
    rs::editor::Editor& gEditor = ctx.editor;
    const UINT gCurWidth = ctx.width;
    const UINT gCurHeight = ctx.height;
    rs::core::Ray ray;
    if (!rs::core::ScreenToRay(mouse.x, mouse.y, vpr.viewProj, gCurWidth, gCurHeight, ray)) return false;
    // 区間編集モード。選択edge上の区間選択と境界ドラッグを扱い、分割モード中だけ
    // 境界の無い位置で分割する。placementAddMode/wireEditModeと同じくこのモード中は
    // 通常のnode/edge/配置物選択を行わない。
    gEditor.sectionEditUi.splitHoverValid = false;
    if (gEditor.SectionTargetEdge() != rs::world::NilGuid()) {
        double hoverS = 0.0;
        if (gEditor.ProjectSOnEdge(gEditor.SectionTargetEdge(), ray, mouse.x, mouse.y, vpr.viewProj,
                                    gCurWidth, gCurHeight, hoverS)) {
            const auto edgeIt = gEditor.graph.edges.find(gEditor.SectionTargetEdge());
            if (edgeIt != gEditor.graph.edges.end() && !edgeIt->second.spline.empty()) {
                const double edgeLen =
                    static_cast<double>(edgeIt->second.spline.back().s);
                hoverS = std::clamp(hoverS, 0.0, edgeLen);
                if (gEditor.sectionEditUi.boundaryDragActive) {
                    // 掴んでいる間はゲート無しでedgeへ追従させ続ける。
                    gEditor.sectionEditUi.boundaryDragPreviewS = hoverS;
                } else {
                    const rs::world::TrackFrame frame =
                        rs::world::EvaluateAt(edgeIt->second, hoverS, +1);
                    const rs::core::Vec3 hoverWorld =
                        frame.pos + frame.up * edgeIt->second.railTopLift;
                    ImVec2 hoverScreen;
                    const rs::core::ProjectedPoint pp = rs::core::ProjectToScreen(
                        hoverWorld, vpr.viewProj, gCurWidth, gCurHeight);
                    if (pp.Visible()) {
                        const float dx = mouse.x - pp.x;
                        const float dy = mouse.y - pp.y;
                        if (std::sqrt(dx * dx + dy * dy) <= 20.0f) {
                            gEditor.sectionEditUi.splitHoverValid = true;
                            gEditor.sectionEditUi.splitHoverS = hoverS;
                        }
                    }
                }
            }
        }
    }

    // ドラッグ中に選択edgeが変わったら、境界の添字は現在のedgeでは
    // 別物を指すので確定させずに破棄する。
    if (gEditor.sectionEditUi.boundaryDragActive &&
        gEditor.sectionEditUi.boundaryDragEdge != gEditor.SectionTargetEdge()) {
        gEditor.CancelSectionBoundaryDrag();
    }

    if (gEditor.sectionEditUi.boundaryDragActive) {
        if (ctx.input.ButtonReleased(ctx.input.ButtonFor(rs::app::input::PointerAction::Select))) {
            gEditor.MoveSelectedEdgeSectionBoundary(gEditor.sectionLayer,
                                                     gEditor.sectionEditUi.boundaryDragIndex,
                                                     gEditor.sectionEditUi.boundaryDragPreviewS);
            gEditor.CancelSectionBoundaryDrag();
        }
        if (ctx.input.ButtonTapped(ctx.input.ButtonFor(rs::app::input::PointerAction::Cancel))) {
            gEditor.CancelSectionBoundaryDrag();
        }
        return true;
    } else if (leftClickForEditor) {
        if (gEditor.hoveredNode != rs::world::NilGuid()) return false;
        std::vector<SectionBoundaryPoint> boundaries = CollectSectionBoundaries(
            gEditor.graph, gEditor.SectionTargetEdge(), gEditor.sectionLayer);
        std::size_t pickedIndex = 0;
        if (PickSectionBoundary(boundaries, vpr, gCurWidth, gCurHeight, mouse.x, mouse.y,
                                10.0f, pickedIndex)) {
            double pickedS = 0.0;
            for (const SectionBoundaryPoint& p : boundaries) {
                if (p.index == pickedIndex) { pickedS = p.s; break; }
            }
            gEditor.sectionEditUi.boundaryDragActive = true;
            gEditor.sectionEditUi.boundaryDragEdge = gEditor.SectionTargetEdge();
            gEditor.sectionEditUi.boundaryDragIndex = pickedIndex;
            gEditor.sectionEditUi.boundaryDragPreviewS = pickedS;
            gEditor.SetSelectedSectionIndex(gEditor.sectionLayer, pickedIndex);
            return true;
        } else if (gEditor.sectionEditMode && !gEditor.sectionSplitMode &&
                   gEditor.sectionEditUi.splitHoverValid) {
            const auto edgeIt = gEditor.graph.edges.find(gEditor.SectionTargetEdge());
            if (edgeIt != gEditor.graph.edges.end()) {
                const std::size_t index = rs::editor::EditorSectionEdit::SectionIndexAt(
                    edgeIt->second, gEditor.sectionLayer, gEditor.sectionEditUi.splitHoverS);
                gEditor.SetSelectedSectionIndex(gEditor.sectionLayer, index);
                return true;
            }
            return true;
        } else if (gEditor.sectionSplitMode && gEditor.sectionEditUi.splitHoverValid) {
            if (gEditor.SplitSelectedEdgeSectionAt(gEditor.sectionLayer,
                                                    gEditor.sectionEditUi.splitHoverS)) {
                const auto edgeIt = gEditor.graph.edges.find(gEditor.SectionTargetEdge());
                if (edgeIt != gEditor.graph.edges.end()) {
                    const std::size_t newIndex = rs::editor::EditorSectionEdit::SectionIndexAt(
                        edgeIt->second, gEditor.sectionLayer, gEditor.sectionEditUi.splitHoverS);
                    gEditor.SetSelectedSectionIndex(gEditor.sectionLayer, newIndex);
                }
            }
            return true;
        } else if (gEditor.sectionSplitMode && gEditor.hoveredEdge != rs::world::NilGuid()) {
            // 選択edgeから離れた位置で別のedgeをクリックしたら、そちらへ
            // 選択を移す(このモード中は通常のedge選択が働かないため、
            // ここで拾わないとモードを一旦切らないとedgeを変えられない)。
            gEditor.SelectEdge(gEditor.hoveredEdge);
            return true;
        }
    }
    return gEditor.sectionSplitMode;
}

void HandlePlacementAddModeInput(const EditorViewportContext& ctx,
                                 const rs::app::ViewProjResult& vpr, rs::app::input::PointerPos mouse,
                                 bool leftClickForEditor) {
    rs::editor::Editor& gEditor = ctx.editor;
    const UINT gCurWidth = ctx.width;
    const UINT gCurHeight = ctx.height;
    rs::core::Ray ray;
    if (!rs::core::ScreenToRay(mouse.x, mouse.y, vpr.viewProj, gCurWidth, gCurHeight, ray)) return;
    // 手動追加モード。Railモードでの配置物(Pier/Pole)の選択/ドラッグは
    // このモード中のみ有効にする(通常のnode/edge編集中に配置物を誤って
    // 掴んでしまわないようにするため)。
    // 配置物ヒット時は選択/ドラッグを優先し(Sceneryモードの「配置物ヒット
    // 優先」規約と統一)、ヒットしなかった場合のみedgeクリックで新規追加する。
    const bool placementConsumed =
        HandlePlacementPickAndDrag(ctx, vpr, mouse, leftClickForEditor);
    if (leftClickForEditor && !placementConsumed && !gEditor.placementDragUi.active &&
        !gEditor.placementAddFolder.empty()) {
        double s = 0.0;
        rs::world::Guid edge = gEditor.PickEdgeS(ray, mouse.x, mouse.y, vpr.viewProj,
                                                  gCurWidth, gCurHeight, s);
        if (edge != rs::world::NilGuid()) {
            // 複線一括仕様(trackNum>1)は自動配置と同じく中心線edgeへ
            // retargetする。実レールへそのままアンカーすると
            // 配線(WalkPoleRuns)や複線ルーティングに乗らない
            // (複線用架線柱=PoleInfo.TrackNum=2が該当する)。
            const int trackNum = ResolveFolderTrackNum(gEditor.placementAddFolder,
                                                       ctx.selector.TrackPluginEntries());
            if (trackNum > 1) {
                const rs::world::Guid centerline = rs::app::ResolveCenterlineEdgeFor(
                    gEditor.graph, edge, trackNum);
                double centerS = 0.0;
                if (centerline != rs::world::NilGuid() &&
                    gEditor.ProjectSOnEdge(centerline, ray, mouse.x, mouse.y, vpr.viewProj,
                                            gCurWidth, gCurHeight, centerS)) {
                    edge = centerline;
                    s = centerS;
                } else {
                    // 対応する中心線が無い(単線レイアウト等)/投影不能なら
                    // 自動配置と同様に配置しない(実レールへの誤配置を防ぐ)。
                    edge = rs::world::NilGuid();
                }
            }
        }
        if (edge != rs::world::NilGuid()) {
            const PlacementAttrs attrs = ResolvePlacementAttrs(
                gEditor.placementAddFolder, edge, gEditor.graph,
                ctx.selector.TrackPluginEntries());
            const std::vector<rs::world::MaterializeSnapshot> snapshots =
                BuildMaterializeSnapshotsForEdit(gEditor.graph, ctx.selector.TrackPluginEntries(),
                                                 {edge}, gEditor.placementAddFolder);
            gEditor.AddManualPlacement(edge, s, gEditor.placementAddFolder,
                                        attrs.keepUpright, attrs.verticalOffsetM, snapshots);
        }
    }
}

// 駅舎アンカーを置くedgeと位置(s)をマウス位置から決める。プレビューと確定で同じ結果を使う。
static bool ResolveStationAnchorTarget(const EditorViewportContext& ctx, const rs::app::ViewProjResult& vpr,
                                       rs::app::input::PointerPos mouse, rs::world::Guid& outEdge,
                                       double& outS) {
    rs::editor::Editor& gEditor = ctx.editor;
    rs::core::Ray ray;
    if (!rs::core::ScreenToRay(mouse.x, mouse.y, vpr.viewProj, ctx.width, ctx.height, ray)) return false;
    double pickedS = 0.0;
    const rs::world::Guid edge = gEditor.PickEdgeS(ray, mouse.x, mouse.y, vpr.viewProj, ctx.width,
                                                     ctx.height, pickedS);
    if (edge == rs::world::NilGuid()) return false;
    const std::unordered_map<rs::world::Guid, rs::world::TrackEdge, rs::world::GuidHash>::const_iterator
        edgeIt = gEditor.graph.edges.find(edge);
    if (edgeIt == gEditor.graph.edges.end()) return false;
    const rs::world::TrackFrame pickedFrame = rs::world::EvaluateAt(edgeIt->second, pickedS, +1);
    float lateralOffsetM = 0.0f;
    if (!gEditor.ProjectPlacementOffsetOnEdge(edge, mouse.x, mouse.y, vpr.viewProj, ctx.width,
                                                ctx.height, pickedFrame.pos.y, 0.0f, outS,
                                                lateralOffsetM)) {
        return false;
    }
    outEdge = edge;
    return true;
}

void HandleStationAnchorModeInput(const EditorViewportContext& ctx,
                                  const rs::app::ViewProjResult& vpr, rs::app::input::PointerPos mouse,
                                  bool leftClickForEditor) {
    rs::editor::Editor& gEditor = ctx.editor;
    rs::world::Guid edge = rs::world::NilGuid();
    double s = 0.0;
    if (gEditor.stationAnchorFolder.empty() || !ResolveStationAnchorTarget(ctx, vpr, mouse, edge, s)) {
        ctx.sceneRenderer.SetPlacementPreview(ctx.app, std::nullopt, ctx.log);
        return;
    }
    // 向きは最後に置いた信号・駅舎に合わせ、Shiftを押している間だけ逆にする。
    const int direction = gEditor.ResolvePlaceTravelDirection(
        edge, ctx.input.MatchesPointer(rs::app::input::PointerAction::PlacementReverse), &ctx.layoutSession.warpLinks, ctx.layoutSession.activeIndex);
    const std::optional<rs::world::FreePlacement> placement =
        gEditor.StationAnchorPlacement(edge, s, direction);
    if (!placement.has_value()) {
        ctx.sceneRenderer.SetPlacementPreview(ctx.app, std::nullopt, ctx.log);
        return;
    }
    const rs::world::ScenePlacement scenePlacement = *placement;
    ctx.sceneRenderer.SetPlacementPreview(
        ctx.app, SceneRenderer::PlacementPreviewSpec{gEditor.stationAnchorFolder,
                                                      rs::world::ResolveWorldMatrix(scenePlacement, gEditor.graph)},
        ctx.log);

    if (!leftClickForEditor) return;
    if (gEditor.AddStationAnchor(ctx.sceneRegistry, edge, s, direction, gEditor.stationAnchorFolder)) {
        gEditor.stationAnchorMode = false;
        gEditor.stationAnchorFolder.clear();
        ctx.sceneRenderer.SetPlacementPreview(ctx.app, std::nullopt, ctx.log);
    }
}

void HandleSelectModeInput(const EditorViewportContext& ctx, const rs::app::ViewProjResult& vpr,
                           rs::app::input::PointerPos mouse, bool leftClickForEditor) {
    rs::editor::Editor& gEditor = ctx.editor;
    const rs::app::input::InputSource& input = ctx.input;
    // press-drag-release(接線ハンドルとノード本体のドラッグ)。押した瞬間に掴んだ対象を
    // 記録し、ドラッグの開始・更新・確定はその記録で処理する(NodeDragInput.h)。
    // このフレームで消費した場合は、以下の配置物/node/edge選択を行わない
    // (掴んでいる間・掴んだ瞬間に選択状態を変えたくないため)。
    LatchSelectPress(gEditor, input, leftClickForEditor);
    const bool tangentDragConsumedFrame = UpdateTangentDrag(ctx, vpr, mouse);
    const bool nodeDragConsumedFrame = UpdateNodeDrag(ctx, vpr, mouse);

    // 空き地を押したフレームだけ真。矩形選択の待ちに入る合図。
    bool pressedOnEmpty = false;
    rs::world::Guid pickHoveredEdge = rs::world::NilGuid();
    if (!nodeDragConsumedFrame && !tangentDragConsumedFrame) {
        // 自動配置物(Pier/Pole)のホバー/選択/ドラッグ移動は
        // PlacementDragInput.cppにある(Sceneryモードからも共通で呼ぶため)。
        // Railモードでの配置物選択は配置物追加モード中のみ
        // 有効にしているため、ここ(通常のSelect、追加モードoff)では
        // 呼ばない。ただし追加モードをドラッグ中に解除された場合に備え、
        // ドラッグ中(placementDragUi.active)だけは引き続き処理して正しく確定/
        // 解放させる(でないと掴んだまま操作不能な状態が残ってしまう)。
        bool placementConsumed = false;
        if (gEditor.placementDragUi.active) {
            placementConsumed = HandlePlacementPickAndDrag(ctx, vpr, mouse, leftClickForEditor);
        } else {
            gEditor.hoveredPlacementEdge = rs::world::NilGuid();
            gEditor.hoveredPlacementFolder.clear();
            gEditor.ClearSelectedPlacement();
        }
        // 接線ハンドルを押したときは、選択を変えない(ハンドルは選択中ノードのものなので、
        // ここで外すとドラッグを始められない)。
        const bool pressedTangentHandle =
            gEditor.selectPress.target == rs::editor::PressTarget::TangentHandle;
        // Shift+クリックの対象は、接線ハンドルや配置物を操作中でなく、ノードに乗っていないときのレール。
        if (!pressedTangentHandle && !placementConsumed && gEditor.hoveredNode == rs::world::NilGuid()) {
            pickHoveredEdge = gEditor.hoveredEdge;
        }
        if (input.MatchesPointer(rs::app::input::PointerAction::SelectionExtend) && input.ButtonTapped(input.ButtonFor(rs::app::input::PointerAction::Select)) &&
            pickHoveredEdge != rs::world::NilGuid()) {
            gEditor.ClearSelectedSceneEntity();
        } else if (leftClickForEditor && !pressedTangentHandle) {
            if (placementConsumed) {
                // 配置物ヒット時はnode/edge選択より優先する(小さい的なので
                // 奥にあるnode/edgeより手前の意図を汲む)。
                gEditor.selectedNode = rs::world::NilGuid();
                gEditor.ClearSelectedEdge();
            } else {
                // ホバーは node > edge 優先。node をホバー中なら node 選択、
                // そうでなければ edge 選択（縦断編集のため node 選択を追加）。
                gEditor.selectedNode = gEditor.hoveredNode;
                gEditor.SelectEdge(gEditor.hoveredEdge);
            }
            gEditor.ClearSelectedSceneEntity();
            pressedOnEmpty = !placementConsumed && !input.MatchesPointer(rs::app::input::PointerAction::SelectionExtend) &&
                             gEditor.hoveredNode == rs::world::NilGuid() &&
                             gEditor.hoveredEdge == rs::world::NilGuid();
        }
    }
    EdgePickInput pick;
    pick.hoveredEdge = pickHoveredEdge;
    pick.pressedOnEmpty = pressedOnEmpty;
    const EdgePickEvent pickEvent = UpdateEdgePick(ctx, vpr, mouse, pick);
    ApplyEdgePickToSelection(gEditor, pickEvent);
    if (pickEvent.kind == EdgePickEvent::Kind::Box) gEditor.ClearSelectedNode();

    // ボタンを離したら、押した対象の記録を捨てる。
    if (input.ButtonReleased(input.ButtonFor(rs::app::input::PointerAction::Select))) gEditor.selectPress.Clear();
}

} // namespace rs::app

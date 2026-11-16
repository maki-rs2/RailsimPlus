#include "ViewportInput.h"

#include <cfloat>
#include <cstring>
#include <string>
#include <vector>

#include "../../core/Math.h"
#include "../../editor/tool/Editor.h"
#include "../../world/scene/SceneRegistry.h"
#include "../action/ToolCancelAction.h"
#include "../scene/ManualPlacementOverlayApply.h"
#include "../scene/PlacementMaterialize.h"
#include "../scene/TrackPluginPlacement.h"
#include "../scene/ScenePicking.h"
#include "../scene/SceneRenderer.h"
#include "PlacementDragInput.h"
#include "ViewportInputInternal.h"
#include "../input/EscapeRouting.h"
#include "ViewportPierBatchPlan.h"

namespace rs::app {

bool IsMouseInViewport(rs::app::input::PointerPos mouse, UINT width, UINT height) {
    return mouse.x >= 0.0f && mouse.x < static_cast<float>(width) && mouse.y >= 0.0f &&
           mouse.y < static_cast<float>(height);
}

namespace {

} // namespace

// ViewportInput.hで公開(PlacementDragInput.cppのクロスエッジ移動
// 候補edgeごとの再解決から使うため、他のPickTrackPluginPlacement等と同じく
// 匿名namespaceの外へ出した)。
PlacementAttrs ResolvePlacementAttrs(const std::string& folderUtf8, const rs::world::Guid& edge,
                                     const rs::world::TrackGraph& graph,
                                     const std::vector<rs::app::TrackPluginEntry>& entries) {
    PlacementAttrs attrs;
    const rs::app::TrackPluginEntry* self = nullptr;
    for (const rs::app::TrackPluginEntry& e : entries) {
        if (e.folderUtf8 == folderUtf8) { self = &e; break; }
    }
    if (!self) return attrs;
    if (self->type == rs::legacy_pi::PluginType::Pier) {
        attrs.keepUpright = true;
    } else if (self->type == rs::legacy_pi::PluginType::Pole) {
        attrs.keepUpright = true;
        const auto edgeIt = graph.edges.find(edge);
        if (edgeIt != graph.edges.end()) {
            for (const rs::app::TrackPluginEntry& lineEntry : entries) {
                if (lineEntry.type != rs::legacy_pi::PluginType::Line) continue;
                if (!lineEntry.def.line.hasLineInfo) continue;
                const bool onEdge =
                    rs::world::EdgeHasFolder(edgeIt->second, lineEntry.folderUtf8);
                if (!onEdge) continue;
                attrs.verticalOffsetM = lineEntry.def.line.trolleyAlt + lineEntry.def.line.height;
                break;
            }
        }
    }
    return attrs;
}

namespace {

// itemのモデル実寸ローカルAABB(sceneRenderer側でfolderUtf8単位に1回だけ計算・
// キャッシュ済み、SceneRenderer::TryGetTrackPluginAssemblyMesh参照)を
// ScreenPickCandidateへ変換する(配置ピッキングはモデル実寸のバウンディングを使う)。メッシュ未取得/空(読込失敗等)ならhasAabb=falseの
// ままとし、呼び出し側(PickByScreenRect)が原点1点＋固定半径へフォールバックする。
// 橋脚は描画と同じ規則で扱う: 低い橋脚は作らない(候補から外す)、高さ別メッシュを使う、
// Y平行移動は地面にする。outSkipがtrueなら描画されない橋脚なので、呼び出し側は候補に入れない。
ScreenPickCandidate MakeTrackPluginPickCandidate(const rs::core::Mat4& worldMatrix,
                                                const std::string& folderUtf8,
                                                const PluginSelector& selector,
                                                SceneRenderer& sceneRenderer,
                                                std::vector<std::string>& log, bool& outSkip) {
    ScreenPickCandidate cand;
    cand.worldMatrix = worldMatrix;
    outSkip = false;
    const rs::render::Mesh* mesh = nullptr;
    bool meshFound = false;
    const TrackPluginEntry* entry = selector.FindTrackPluginEntry(folderUtf8);
    if (entry != nullptr && entry->type == rs::legacy_pi::PluginType::Pier) {
        const PierBatchPlan plan = ComputePierBatchPlan(folderUtf8, entry, worldMatrix);
        if (plan.skip) {
            outSkip = true;
            return cand;
        }
        ZeroPierWorldY(cand.worldMatrix);
        meshFound = sceneRenderer.TryGetPierAssemblyMesh(folderUtf8, plan.roundedHeight, log, &mesh);
    } else {
        meshFound = sceneRenderer.TryGetTrackPluginAssemblyMesh(folderUtf8, log, &mesh);
    }
    if (meshFound && mesh && !mesh->vertices.empty()) {
        cand.hasAabb = true;
        std::memcpy(cand.aabbMin, mesh->aabbMin, sizeof(cand.aabbMin));
        std::memcpy(cand.aabbMax, mesh->aabbMax, sizeof(cand.aabbMax));
    }
    return cand;
}

// Scenery(駅舎/施設等、SceneEntityCategory::Scenery)のホバー/選択ピッキング。
// ScenePicking.hの共通ヒットテスト(PickByScreenRect)へ、SceneRenderer側で
// 列挙したScenePickItem(entityId+ワールド行列+モデル実寸AABB)を渡すだけの薄い
// ラッパー。
} // namespace

bool PickSceneEntity(SceneRenderer& sceneRenderer, const rs::world::SceneRegistry& registry,
                    const rs::world::TrackGraph& graph, std::vector<std::string>& log,
                    const rs::core::Ray& ray, float screenX, float screenY,
                    const rs::core::Mat4& viewProj, UINT width, UINT height, float pxRadius,
                    rs::world::Guid& outEntityId) {
    std::vector<SceneRenderer::ScenePickItem> items;
    sceneRenderer.CollectSceneryPickCandidates(registry, graph, log, items);
    std::vector<ScreenPickCandidate> candidates;
    candidates.reserve(items.size());
    for (const SceneRenderer::ScenePickItem& item : items) candidates.push_back(item.candidate);
    size_t idx = 0;
    if (!PickByRay(candidates, ray, FLT_MAX, viewProj, width, height, screenX, screenY, pxRadius,
                  idx))
        return false;
    outEntityId = items[idx].entityId;
    return true;
}

// Train(編成車両、SceneEntityCategory::Car)のホバーピッキング(ハイライト
// 表示専用、選択/削除機能は持たない。車両選択はホバーで行わず、
// 配置プラグインのスイッチ編集で足りるため、ここではハイライトだけを用意する)。PickSceneEntityと同じ薄いラッパー構造。
namespace {

bool PickTrainVehicle(SceneRenderer& sceneRenderer, const rs::world::SceneRegistry& registry,
                        const rs::core::Ray& ray, float screenX, float screenY,
                        const rs::core::Mat4& viewProj, UINT width, UINT height, float pxRadius,
                        rs::world::Guid& outEntityId) {
    std::vector<SceneRenderer::ScenePickItem> items;
    sceneRenderer.CollectTrainPickCandidates(registry, items);
    std::vector<ScreenPickCandidate> candidates;
    candidates.reserve(items.size());
    for (const SceneRenderer::ScenePickItem& item : items) candidates.push_back(item.candidate);
    size_t idx = 0;
    if (!PickByRay(candidates, ray, FLT_MAX, viewProj, width, height, screenX, screenY, pxRadius,
                  idx))
        return false;
    outEntityId = items[idx].entityId;
    return true;
}

} // namespace

bool PickTrackPluginPlacement(const std::vector<TrackPluginPlacementItem>& items,
                             const rs::world::TrackGraph& graph, const PluginSelector& selector,
                             SceneRenderer& sceneRenderer,
                             std::vector<std::string>& log, const rs::core::Ray& ray,
                             float screenX, float screenY, const rs::core::Mat4& viewProj,
                             UINT width, UINT height, float pxRadius, size_t& outIndex) {
    std::vector<ScreenPickCandidate> candidates;
    // candidatesの添字からitemsの添字へ戻す表(描画されない橋脚は候補に入れない)。
    std::vector<size_t> itemIndices;
    candidates.reserve(items.size());
    itemIndices.reserve(items.size());
    for (size_t i = 0; i < items.size(); ++i) {
        const rs::core::Mat4 m = rs::world::ResolveWorldMatrix(items[i].placement, graph);
        bool skip = false;
        ScreenPickCandidate cand =
            MakeTrackPluginPickCandidate(m, items[i].folderUtf8, selector, sceneRenderer, log, skip);
        if (skip) continue;
        candidates.push_back(cand);
        itemIndices.push_back(i);
    }
    size_t candidateIndex = 0;
    if (!PickByRay(candidates, ray, FLT_MAX, viewProj, width, height, screenX, screenY, pxRadius,
                   candidateIndex))
        return false;
    outIndex = itemIndices[candidateIndex];
    return true;
}

// マウス/キーボード入力によるnode/edge/配置物の選択・ドラッグ・Undo/Redo・削除を処理する。
void HandleEditorInput(const EditorViewportContext& ctx, const rs::app::ViewProjResult& vpr,
                      rs::app::input::PointerPos mouse, bool editingEnabled) {
    rs::editor::Editor& gEditor = ctx.editor;
    gEditor.BindSceneRegistry(ctx.sceneRegistry);
    const rs::app::input::InputSource& input = ctx.input;
    const UINT gCurWidth = ctx.width;
    const UINT gCurHeight = ctx.height;

    // 区間境界ドラッグは下の区間分割モードの分岐でしか終了処理を持たないため、
    // 掴んだまま別モードへ移った/編集が無効になった場合はここで中断させる。
    // 条件は下のHandleSectionSplitModeInputへ入る条件の否定形になっている
    // (二重管理)。片方の条件を変えたら必ずもう片方も直すこと。
    if (gEditor.sectionEditUi.boundaryDragActive &&
        !(editingEnabled && gEditor.mode == rs::editor::ToolMode::Select &&
          (gEditor.sectionSplitMode || gEditor.sectionEditMode))) {
        gEditor.CancelSectionBoundaryDrag();
    }

    // Shiftを押している間だけ角度スナップを有効にする(常時ONの設定とは別)。
    gEditor.layingSettings.angleSnapHeld =
        editingEnabled && gEditor.mode == rs::editor::ToolMode::PlaceArc && input.MatchesPointer(rs::app::input::PointerAction::AngleSnap);
    // Spaceを押している間は敷設のスナップを解除する(敷設ツールだけ)。
    gEditor.layingSettings.snapDisabledHeld =
        editingEnabled && gEditor.mode == rs::editor::ToolMode::PlaceArc &&
        input.Held(rs::app::input::InputAction::EditorSnapDisable);

    bool boxSelectHandled = false;
    if (editingEnabled && input.AllowsPointer(rs::app::input::InputContext::Edit) && IsMouseInViewport(mouse, gCurWidth, gCurHeight)) {
        rs::core::Ray ray;
        if (!rs::core::ScreenToRay(mouse.x, mouse.y, vpr.viewProj, gCurWidth, gCurHeight, ray)) return;
        gEditor.UpdateHover(ray, mouse.x, mouse.y, vpr.viewProj, gCurWidth, gCurHeight);
        // Shift+左ドラッグはカメラの高さ調節に割り当てているため、Shift押下中の
        // 左クリックはエディタ操作として扱わない。
        // 敷設ツールだけは、Shift押下中のクリックもエディタへ渡す(Shiftは角度スナップの
        // 一時有効で、押したまま確定できないと使えないため)。Alt+クリックはedge分割に使う。
        const bool leftClicked = input.ButtonClicked(input.ButtonFor(rs::app::input::PointerAction::Select));
        const bool altDown = input.MatchesPointer(rs::app::input::PointerAction::EdgeSplit);
        // 駅舎アンカーを置くときも、Shiftは向きの反転に使うため押したまま確定できるようにする。
        const bool stationAnchorPlacing =
            gEditor.mode == rs::editor::ToolMode::Select && !gEditor.sectionSplitMode && !gEditor.sectionEditMode &&
            gEditor.stationAnchorMode;
        const bool shiftBlocksClick = input.MatchesPointer(rs::app::input::PointerAction::CameraHeight) &&
                                      gEditor.mode != rs::editor::ToolMode::PlaceArc &&
                                      !stationAnchorPlacing;
        const bool leftClickForEditor = leftClicked && !altDown && !shiftBlocksClick;
        if (leftClicked && altDown && gEditor.CanAltSplit()) {
            gEditor.SplitEdgeAtScreen(mouse.x, mouse.y, vpr.viewProj, gCurWidth, gCurHeight);
        }
        if (gEditor.mode == rs::editor::ToolMode::PlaceArc) {
            HandlePlaceArcModeInput(ctx, vpr, mouse, leftClickForEditor);
        } else if (gEditor.mode == rs::editor::ToolMode::Select && gEditor.sectionSplitMode) {
            HandleSectionSplitModeInput(ctx, vpr, mouse, leftClickForEditor);
        } else if (gEditor.mode == rs::editor::ToolMode::Select && gEditor.sectionEditMode &&
                   !gEditor.placementAddMode && !gEditor.wireEditMode) {
            const bool sectionConsumed = HandleSectionSplitModeInput(ctx, vpr, mouse, leftClickForEditor);
            if (!sectionConsumed) {
                HandleSelectModeInput(ctx, vpr, mouse, leftClickForEditor);
                if (leftClickForEditor && gEditor.selectedEdge != rs::world::NilGuid()) {
                    double s = 0.0;
                    if (gEditor.ProjectSOnEdge(gEditor.selectedEdge, ray, mouse.x, mouse.y,
                                               vpr.viewProj, gCurWidth, gCurHeight, s)) {
                        const auto edge = gEditor.graph.edges.find(gEditor.selectedEdge);
                        if (edge != gEditor.graph.edges.end()) {
                            const std::size_t index = rs::editor::EditorSectionEdit::SectionIndexAt(
                                edge->second, gEditor.sectionLayer, s);
                            gEditor.SetSelectedSectionIndex(gEditor.sectionLayer, index);
                        }
                    }
                }
                boxSelectHandled = true;
            }
        } else if (gEditor.mode == rs::editor::ToolMode::Select && gEditor.stationAnchorMode) {
            HandleStationAnchorModeInput(ctx, vpr, mouse, leftClickForEditor);
        } else if (gEditor.mode == rs::editor::ToolMode::Select && gEditor.sceneryPlacementMode) {
            HandleSceneryPlacementModeInput(ctx, vpr, mouse, leftClickForEditor);
        } else if (gEditor.mode == rs::editor::ToolMode::Select && gEditor.placementAddMode) {
            HandlePlacementAddModeInput(ctx, vpr, mouse, leftClickForEditor);
        } else if (gEditor.mode == rs::editor::ToolMode::Select && gEditor.wireEditMode) {
            HandleWireSpanModeInput(ctx, vpr, mouse, leftClickForEditor);
        } else if (gEditor.mode == rs::editor::ToolMode::Select) {
            HandleSelectModeInput(ctx, vpr, mouse, leftClickForEditor);
            boxSelectHandled = true;
        }
    } else if (editingEnabled && input.AllowsPointer(rs::app::input::InputContext::Edit)) {
        // マウスがビューポート外(ウィンドウ外)にある間はホバーを反応させない
        // (IsMouseInViewport不成立)。前フレームのハイライトが残らないよう消す。
        gEditor.hoveredNode = rs::world::NilGuid();
        gEditor.hoveredEdge = rs::world::NilGuid();
        gEditor.hoveredPlacementEdge = rs::world::NilGuid();
        gEditor.hoveredPlacementFolder.clear();
        if (gEditor.sceneryPlacementMode || gEditor.wireEditMode || gEditor.stationAnchorMode) {
            ctx.sceneRenderer.SetPlacementPreview(ctx.app, std::nullopt, ctx.log);
        }
    }
    // マウスがUIの上にある間(入力がエディタに渡らない間)も、画面外と同じにプレビューを消す。
    // 上の2つの分岐は、ポインタがエディタへ渡る場合だけ入るため、ここで拾う。
    const bool pointerOnEditorViewport = editingEnabled && input.AllowsPointer(rs::app::input::InputContext::Edit) &&
                                         IsMouseInViewport(mouse, gCurWidth, gCurHeight);
    if (!pointerOnEditorViewport && (gEditor.sceneryPlacementMode || gEditor.wireEditMode || gEditor.stationAnchorMode)) {
        ctx.sceneRenderer.SetPlacementPreview(ctx.app, std::nullopt, ctx.log);
    }
    // 矩形選択の待ち・描画中に、選択モード以外へ移った・UIの上で離したなどで入力処理が
    // 走らなかったときは、状態を残さない。信号・ダイヤのモードは、自分の入力処理が待ち状態を持つ。
    const bool ownsBoxSelect = GetViewModeTraits(ctx.viewMode).ownsBoxSelect;
    if (gEditor.boxSelectUi.armed && !ownsBoxSelect &&
        (!editingEnabled || gEditor.mode != rs::editor::ToolMode::Select ||
         input.ButtonReleased(input.ButtonFor(rs::app::input::PointerAction::Select))) &&
        !(editingEnabled && gEditor.mode == rs::editor::ToolMode::Select && boxSelectHandled)) {
        gEditor.boxSelectUi.Clear();
    }
    if (GetViewModeTraits(ctx.viewMode).editorCommands) {
        // Escはワークスペース側で処理するときは読まない(ctx.escapeHandledByWorkspace)。
        if (!ctx.escapeHandledByWorkspace &&
            input.Triggered(rs::app::input::InputAction::EditorCancel)) {
            rs::app::input::EscapeHandlers handlers{};
            handlers[workspace::EnumIndex(rs::app::input::EscapeTarget::Tool)] = [&] {
                ToolCancelAction(gEditor, ctx.viewMode).CancelOperation();
                return true;
            };
            rs::app::input::DispatchEscape(handlers);
        }
        // Del: 選択要素を削除。配置物(Pier/Pole) > node > edge の優先度
        // (配置物選択時はnode/edgeが排他的にクリアされているため、実質競合しない)。
        if (input.Triggered(rs::app::input::InputAction::EditorDelete)) {
            // 架線モードで径間を選んでいるときだけ、径間の削除を優先する。
            if (gEditor.wireEditMode && gEditor.wireSelectedSpan.has_value()) {
                DeleteSelectedWireSpan(ctx);
            } else if (GetViewModeTraits(ctx.viewMode).scenery && gEditor.HasSelectedSceneEntity()) {
                gEditor.DeleteSelectedSceneEntities(ctx.sceneRegistry);
            } else if (gEditor.HasSelectedPlacement()) {
                const std::vector<rs::world::MaterializeSnapshot> snapshots =
                    BuildMaterializeSnapshotsForEdit(
                        gEditor.graph, ctx.selector.TrackPluginEntries(),
                        {gEditor.selectedPlacementEdge}, gEditor.selectedPlacementFolder);
                gEditor.DeleteSelectedPlacement(snapshots);
            }
            else if (gEditor.selectedNode != rs::world::NilGuid())
                gEditor.RemoveSelectedNode(&ctx.layoutSession.warpLinks, ctx.layoutSession.activeIndex);
            else if (gEditor.selectedEdge != rs::world::NilGuid())
                gEditor.RemoveSelectedEdge(&ctx.layoutSession.warpLinks, ctx.layoutSession.activeIndex);
        }
    }
    // 架線編集モード中の半透明プレビューは、モードの入力処理が出し入れする。
    // 駅舎アンカーを置く間の半透明プレビューも、モードの入力処理が出し入れする。
    const bool wirePreviewOwned = editingEnabled && gEditor.wireEditMode;
    const bool stationPreviewOwned = editingEnabled && gEditor.mode == rs::editor::ToolMode::Select &&
                                     !gEditor.sectionSplitMode && !gEditor.sectionEditMode && gEditor.stationAnchorMode;
    if (!gEditor.sceneryPlacementMode && !wirePreviewOwned && !stationPreviewOwned) {
        ctx.sceneRenderer.SetPlacementPreview(ctx.app, std::nullopt, ctx.log);
    }
}

// Train(編成車両)のホバーハイライト判定。Trainモードのみで評価する。RailSim II では
// 「配置・撤去のハイライト」と「配置プラグインのスイッチ編集」が別の選択系統で、
// 後者は全プラグイン種別共通のクリックのみの軽量方式で、別の処理が担う。ホバーハイライトのみを設定し、クリック選択とDelete削除は
// TrainPlacementInput側が担う(ヒット有無を戻り値で渡す)。
bool HandleTrainHoverInput(const EditorViewportContext& ctx, const rs::app::ViewProjResult& vpr,
                             rs::app::input::PointerPos mouse, bool trainHoverEnabled) {
    rs::editor::Editor& gEditor = ctx.editor;
    if (!trainHoverEnabled || !ctx.input.AllowsPointer(rs::app::input::InputContext::Edit)) return false;
    if (!IsMouseInViewport(mouse, ctx.width, ctx.height)) {
        gEditor.hoveredSceneEntityId = rs::world::NilGuid();
        return false;
    }

    rs::world::Guid hoverEntityId = rs::world::NilGuid();
    rs::core::Ray ray;
    if (!rs::core::ScreenToRay(mouse.x, mouse.y, vpr.viewProj, ctx.width, ctx.height, ray)) return false;
    const bool hoverTrain = PickTrainVehicle(ctx.sceneRenderer, ctx.sceneRegistry, ray,
                                                 mouse.x, mouse.y, vpr.viewProj, ctx.width,
                                                 ctx.height, 12.0f, hoverEntityId);
    gEditor.hoveredSceneEntityId = hoverTrain ? hoverEntityId : rs::world::NilGuid();
    return hoverTrain;
}

} // namespace rs::app

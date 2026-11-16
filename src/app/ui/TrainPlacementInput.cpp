//
// TrainPlacementInput.cpp
//
#include "TrainPlacementInput.h"

#include <algorithm>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "../../editor/tool/Editor.h"
#include "../../sim/DiaRuntime.h"
#include "../../world/alignment/FormationRoutineResolve.h"
#include "../../world/alignment/TrackTraversal.h"
#include "../../world/scene/SceneRegistry.h"
#include "../action/FormationAction.h"
#include "../plugin_session/LayoutSession.h"
#include "../plugin_session/PluginSelector.h"
#include "../scene/TrainPlacement.h"
#include "../scene/SceneRenderer.h"
#include "ViewportInput.h"

namespace rs::app {

void HandleTrainPlacementInput(const EditorViewportContext& ctx,
                                 const rs::app::ViewProjResult& vpr, rs::app::input::PointerPos mouse,
                                 bool placementEnabled, bool trainHovered) {
    rs::editor::Editor& gEditor = ctx.editor;
    // ハイライトは毎フレーム作り直す(無効時・早期returnでは消えた状態にする)。
    ctx.trainPlacementUi.preview = TrainPlacementPreview{};
    // 無効時は何も触らない(有効時のみクリアするのは、Railモードでは同じフレームの
    // 先行するHandleEditorInput->UpdateHoverがhoveredEdgeを立てており、ここで消すと
    // 敷設側のホバーハイライトが毎フレーム潰れてしまうため)。モード切替時の消し込みは
    // TickEditorViewport側のクリアブロックが行う。
    if (!placementEnabled) return;
    // 以降は早期returnのどの経路でもホバーが残らないよう、判定前に必ずクリアする。
    gEditor.hoveredEdge = rs::world::NilGuid();

    // 選択中の配置済み編成をDeleteキーで撤去する。テキスト入力中(編成名の
    // InputText等)はキーボードがImGuiに取られているので反応させない。
    if (ctx.input.Triggered(rs::app::input::InputAction::EditorDelete) &&
        gEditor.HasSelectedSceneEntity()) {
        const rs::world::SceneEntity* entity =
            ctx.sceneRegistry.FindEntity(gEditor.selectedSceneEntityId);
        const TrainId trainId = entity != nullptr ? entity->trainId : rs::world::NilGuid();
        if (!rs::world::GuidIsNil(trainId)) {
            // 撤去ボタンと同じ経路(走行UIの行・運転台カメラの追従の後始末を含む)。
            ctx.formationAction.RemoveGroup(trainId);
            gEditor.ClearSelectedSceneEntity();
            gEditor.hoveredSceneEntityId = rs::world::NilGuid();
            ctx.log.push_back("[scene] 編成撤去");
        }
    }

    if (!ctx.input.AllowsPointer(rs::app::input::InputContext::Edit)) return;
    if (!IsMouseInViewport(mouse, ctx.width, ctx.height)) return;

    // マウス直下に配置済み車両があるときは配置せず選択に回す(同じクリックで
    // 車両の下のedgeにも当たるため、配置と選択が同時に走るのを防ぐ)。
    if (trainHovered) {
        if (ctx.input.ButtonClicked(ctx.input.ButtonFor(rs::app::input::PointerAction::Select))) {
            gEditor.SelectSceneEntity(gEditor.hoveredSceneEntityId);
            // 選んだ列車の編成情報カードを広げる。
            const rs::world::SceneEntity* const entity = ctx.sceneRegistry.FindEntity(gEditor.hoveredSceneEntityId);
            if (entity != nullptr && !rs::world::GuidIsNil(entity->trainId)) ctx.trainPlacementUi.focusedTrainId = entity->trainId;
        }
        return;
    }

    const std::vector<FormationCar>& cars = ctx.selector.SelectedFormationCars();
    if (cars.empty()) return;
    // 同じ編成から置いた列車があるうちは置き直せない(プレビューも出さない)。
    const std::vector<Formation>& placedCheckFormations = ctx.selector.Formations();
    const size_t placedCheckIndex = ctx.selector.SelectedFormationIndex();
    if (placedCheckIndex < placedCheckFormations.size() &&
        ctx.sceneRenderer.DiaRuntime().TagRegistry().HasTrainOfFormation(placedCheckFormations[placedCheckIndex].guid)) {
        return;
    }

    rs::core::Ray ray;
    if (!rs::core::ScreenToRay(mouse.x, mouse.y, vpr.viewProj, ctx.width, ctx.height, ray)) return;

    // 複線中心線(centerlineOnly)は実体の走行面を持たないため、実レールだけを
    // ホバー/配置対象にする。
    double s = 0.0;
    const rs::world::Guid picked =
        gEditor.PickEdgeS(ray, mouse.x, mouse.y, vpr.viewProj, ctx.width, ctx.height, s,
                          rs::editor::Editor::kDefaultEdgePickRadiusPx,
                          /*skipCenterlineOnly=*/true);
    if (picked == rs::world::NilGuid()) return;
    gEditor.hoveredEdge = picked;

    // Shiftを押している間だけ、チェック(reversedHead)と逆の向きにする。
    const bool reversed = ctx.trainPlacementUi.reversedHead != ctx.input.MatchesPointer(rs::app::input::PointerAction::PlacementReverse);
    TrainPlacementPreview& preview = ctx.trainPlacementUi.preview;
    preview.head.edge = picked;
    preview.head.s = s;
    preview.head.direction = reversed ? -1 : +1;
    preview.lengthM = 0.0;
    for (const FormationCar& car : cars) {
        preview.lengthM += car.frontLimit - car.tailLimit;
    }
    // 編成が軌道の終端にはみ出す位置、または他の編成と重なる位置は置けない。
    const bool fitsOnTrack = CarsFitOnTrack(cars, preview.head, gEditor.graph);
    preview.blocked = !fitsOnTrack || ctx.sceneRenderer.WouldTrainOverlap(
        ctx.sceneRegistry, gEditor.graph, preview.head, preview.lengthM,
        ctx.layoutSession.activeIndex);
    preview.valid = true;

    if (ctx.input.ButtonClicked(ctx.input.ButtonFor(rs::app::input::PointerAction::Select))) {
        const rs::world::TrackPosition head = preview.head;
        if (preview.blocked) {
            ctx.log.push_back(fitsOnTrack ? "[scene] 編成配置: 0両(衝突のため配置できません)"
                                          : "[scene] 編成配置: 0両(軌道の終端に収まらないため配置できません)");
            return;
        }
        TrainId trainId = rs::world::NilGuid();
        const int placed = PlaceTrainRepeating(ctx.sceneRegistry, cars, head, gEditor.graph,
                                                 ctx.layoutSession.activeIndex, &trainId);
        if (placed > 0) {
            ctx.trainPlacementUi.placedTrainIds.push_back(trainId);
            ctx.trainPlacementUi.focusedTrainId = trainId;
            const std::vector<Formation>& formations = ctx.selector.Formations();
            const size_t selectedFormationIndex = ctx.selector.SelectedFormationIndex();
            if (!formations.empty() && selectedFormationIndex < formations.size()) {
                const rs::world::Guid formationId = formations[selectedFormationIndex].guid;
                rs::world::RegisterPlacedTrainFormation(
                    formationId, trainId,
                    ctx.sceneRenderer.DiaRuntime().TagRegistry());
            }
        }
        ctx.log.push_back("[scene] 編成配置: " + std::to_string(placed) + "両");
    }
}

void SyncTrainPlacementPreview(const EditorViewportContext& ctx) {
    const TrainPlacementPreview& preview = ctx.trainPlacementUi.preview;
    const std::vector<FormationCar>& cars = ctx.selector.SelectedFormationCars();
    // 置けない位置(他の編成との重なり・終端にはみ出し)では車両モデルを出さない。
    // 先頭の横棒(赤)だけが置けないことを示す。
    if (!preview.valid || preview.blocked || cars.empty()) {
        ctx.sceneRenderer.SetTrainPlacementPreview(ctx.app, std::nullopt, ctx.log);
        return;
    }
    SceneRenderer::TrainPreviewSpec spec;
    spec.cars = &cars;
    spec.graph = &ctx.editor.graph;
    spec.head = preview.head;
    ctx.sceneRenderer.SetTrainPlacementPreview(ctx.app, spec, ctx.log);
}

} // namespace rs::app

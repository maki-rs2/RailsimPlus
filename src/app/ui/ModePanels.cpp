#include "ModePanels.h"

#include "imgui.h"
#include "../scene/ParsedPluginCache.h"

#include "../action/EdgePropertyAction.h"
#include "../action/EdgeSectionEditAction.h"
#include "../action/FormationAction.h"
#include "../action/GameSettingsAction.h"
#include "../action/NodePropertyAction.h"
#include "../action/RenderSettingsAction.h"
#include "../action/StationDiaAction.h"
#include "../action/SceneSignalAspectSource.h"
#include "../action/SceneSignalSidecarSource.h"
#include "../action/SignalBlockAction.h"
#include "../camera/CameraControl.h"
#include "../../world/scene/SceneRegistry.h"
#include "../scene/SceneRenderer.h"

namespace rs::app {

void ModePanels::Draw(ViewMode viewMode, const ModePanelsContext& ctx) {
    // どのパネルが開いているかに関わらず、ダイアログの結果はここで毎フレーム受け取る。
    viewportState_.Synchronize(ctx.editor.SceneGeneration(), GetParsedPluginCache().Generation());
    pathPicker_.BeginFrame(ctx.windowHandle);
    const ViewModeTraits& traits = GetViewModeTraits(viewMode);
    if (traits.trackEditing) {
        if (!ctx.viewerMode && !ctx.rmlWorkspace) {
            // --- 敷設エディタパネル ---
            ImGui::SetNextWindowPos(ImVec2(10, 420), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(360, 260), ImGuiCond_FirstUseEver);
            TrackLayingAction trackLayingAction(ctx.editor, ctx.lastArcPreview);
            EdgeSectionEditAction edgeSectionEditAction(ctx.editor);
            EdgePropertyAction edgePropertyAction(ctx.editor, ctx.layoutSession, ctx.selector);
            NodePropertyAction nodePropertyAction(ctx.editor, warpAuthoring_, ctx.layoutSession);
            // ワープ端点の保留中に別のnodeが選ばれたら確定する。パネルの描画関数
            // (折り畳み等で呼ばれないことがある)に頼らず、毎フレームここで判断する。
            nodePropertyAction.AutoConfirmWarpPending();
            DrawEditorPanel({trackLayingAction, edgeSectionEditAction, edgePropertyAction, nodePropertyAction,
                             pathPicker_});

            // --- レール系プラグインリスト ---
            ImGui::SetNextWindowPos(ImVec2(380, 420), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(360, 240), ImGuiCond_FirstUseEver);
            DrawTrackPluginListPanel(ctx.editor, ctx.selector, pathPicker_, railListError_,
                                    railTemplateError_, ctx.sceneRegistry, viewportState_);
        }
    }
    if (traits.train) {
        if (!ctx.viewerMode) {
            // --- 編成リスト(配置+走行) ---
            ImGui::SetNextWindowPos(ImVec2(10, 420), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(360, 240), ImGuiCond_FirstUseEver);
            // 参照束の組み立てはここ1箇所だけ。
            FormationRefs formationRefs{ctx.selector, ctx.sceneRegistry, ctx.sceneRenderer,
                                    trainPlacementUi_, ctx.layoutDocument, ctx.cabinCamera, ctx.camera};
            FormationAction formationAction(formationRefs);
            DrawFormationListPanel({formationAction, formationPanelState_, ctx.selector, pathPicker_});
        }
    }
    if (traits.stationDia) {
        if (!ctx.viewerMode) {
            StationDiaAction stationDiaAction(ctx.editor);
            DrawStationDiaPanel({stationDiaAction, stationDiaPanelState_, ctx.editor, &ctx.sceneRenderer,
                                 trainPlacementUi_});
        }
    }
    if (traits.signalBlock) {
        if (!ctx.viewerMode) {
            SceneSignalSidecarSource sidecarSource(ctx.sceneRenderer);
            SceneSignalAspectSource aspectSource(ctx.sceneRenderer);
            SignalBlockAction signalBlockAction(ctx.editor, sidecarSource, aspectSource);
            DrawSignalBlockPanel({signalBlockAction, pathPicker_});
        }
    }
    if (traits.scenery) {
        if (!ctx.viewerMode) {
            // --- 情景プラグイン(Station/Struct/Surface等)選択 ---
            ImGui::SetNextWindowPos(ImVec2(10, 420), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(360, 240), ImGuiCond_FirstUseEver);
            DrawSingleSlotListPanel(ctx.editor, ctx.selector, pathPicker_, slotListError_, ctx.sceneRegistry,
                                    sceneryPlacementUi_);
        }
    }
    if (traits.gameSettings) {
        // --- ゲーム設定パネル(fps/カメラ操作、両モード共通) ---
        ImGui::SetNextWindowPos(ImVec2(10, 420), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(340, 160), ImGuiCond_FirstUseEver);
        // 参照束の組み立てはここ1箇所だけ。
        GameSettingsRefs gameSettingsRefs{ctx.app,       ctx.camera,       ctx.world,
                                          ctx.localization, ctx.userSettings, ctx.audioEngine, ctx.layoutDocument};
        GameSettingsAction gameSettingsAction(gameSettingsRefs);
        DrawGameSettingsPanel({gameSettingsAction, gameSettingsPanelState_, pathPicker_});

        // --- 描画設定パネル(描画距離/空と光/影)。ゲーム設定と並べて別ウィンドウで出す ---
        ImGui::SetNextWindowPos(ImVec2(360, 420), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(340, 220), ImGuiCond_FirstUseEver);
        RenderSettingsRefs renderSettingsRefs{ctx.app, ctx.camera, ctx.userSettings};
        RenderSettingsAction renderSettingsAction(renderSettingsRefs);
        DrawRenderSettingsPanel({renderSettingsAction, ctx.localization});
    }
    if (traits.saveData) {
        // --- セーブデータパネル(.rs2読込／.rsl保存・読込) ---
        ImGui::SetNextWindowPos(ImVec2(10, 420), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(420, 300), ImGuiCond_FirstUseEver);
        DrawSaveDataPanel({ctx.layoutAction, ctx.selector, saveDataPanelState_, pathPicker_, ctx.loadLog, ctx.localization});
    }
}

} // namespace rs::app

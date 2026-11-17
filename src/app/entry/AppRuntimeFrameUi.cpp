//
// AppRuntimeFrameUi.cpp - RunFrameLoop()のUI関連ステージ(遅延読込・パネル描画・Scene切替)
//
#include "AppRuntimeFrameInternal.h"
#include "LayoutResultReporter.h"
#include "ViewerMode.h"

#include <optional>

#include <spdlog/spdlog.h>

#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"

#include "../action/LayoutPersistenceAction.h"
#include "../action/SceneAction.h"
#include "../ui/EditorViewport.h"
#include "../ui/ImGuiFontManager.h"
#include "../../editor/tool/Editor.h"

namespace rs::app {

namespace {

// 前回の値より増えていたら音を積み、前回の値を更新する。
void PushIfIncreased(AppRuntimeState& state, std::size_t current, std::size_t& last, UiSound sound) {
    if (current > last) state.uiSound.Push(sound);
    last = current;
}

// 操作の効果音(列車の配置と撤去・置く・消す・警告・エラー)を、数の前フレームとの差で決める。
// 入れ方や経路によらず同じ音になる。
void PushOperationSounds(FrameLoopState& loopState) {
    AppRuntimeState& state = loopState.state;
    AppRuntimeState::UiSoundWatch& last = state.lastUiSound;

    const std::size_t placedTrains = state.modePanels.TrainPlacementUi().placedTrainIds.size();
    if (placedTrains < last.placedTrains) state.uiSound.Push(UiSound::TrainRemove);
    PushIfIncreased(state, placedTrains, last.placedTrains, UiSound::TrainPlace);

    const rs::editor::Editor& editor = loopState.globals.editor;
    PushIfIncreased(state, editor.PlacedCount(), last.editorPlaced, UiSound::NodePlace);
    PushIfIncreased(state, editor.RemovedCount(), last.editorRemoved, UiSound::Delete);

    if (rmlui::RmlUiWorkspaceView* workspace = state.rmlUiIntegration.WorkspaceView()) {
        const ToastQueue& toasts = workspace->Toasts();
        PushIfIncreased(state, toasts.PostedCount(ToastSeverity::Warning), last.warnings, UiSound::Warning);
        PushIfIncreased(state, toasts.PostedCount(ToastSeverity::Error), last.errors, UiSound::Error);
    }
}

} // namespace

void RunPluginTickAndFontRebuild(FrameLoopState& loopState) {
    AppRuntimeGlobals& globals = loopState.globals;
    AppRuntimeState& state = loopState.state;
    ImGuiIO& io = loopState.io;

    PushOperationSounds(loopState);
    state.uiSound.Flush(state.audioEngine);
    state.audioEngine.Update();

    // プラグイン読込直後: 稀な漢字も含めてフォントアトラスを再構築する。
    // NewFrame群より前(フレーム外)でのみ Fonts->Clear/Build を行うこと。
    // 開いたプラグインがスロットにあるときは、そのセッションのプラグインから漢字を集める(毎回引き直す)。
    if (state.openedPlugin.fontRebuildNeeded) {
        const PluginSession* opened = FindOpenedPluginSession(state);
        BuildJapaneseFontAtlas(io, globals.fontRanges, opened != nullptr ? &opened->plugin : nullptr,
                               /*rebuildBackend=*/true);
        state.openedPlugin.fontRebuildNeeded = false;
    }
}

void RunDeferredStartupLoadIfDue(FrameLoopState& loopState) {
    AppRuntimeGlobals& globals = loopState.globals;
    AppRuntimeState& state = loopState.state;

    if (state.deferredStartupLoad.pending && loopState.framesBeforeStartupLoad >= 1) {
        const std::chrono::steady_clock::time_point tLoad0 = std::chrono::steady_clock::now();
        RunStartupLoad(globals, state, state.deferredStartupLoad.openPluginPath);
        state.deferredStartupLoad.pending = false;
        spdlog::debug("[startup] 遅延した起動時読込={:.0f}ms (助走{}フレーム後)",
                     std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tLoad0)
                         .count(),
                     loopState.framesBeforeStartupLoad);
    }
}

void TickPendingLayoutLoad(FrameLoopState& loopState) {
    AppRuntimeState& state = loopState.state;
    state.LayoutAction().TickPendingLoad();
    // 予約の読込だけでなく、新規レイアウトの結果もここで受け取る。
    ReportFinishedLayoutResults(state, &loopState.layoutDocument);
}

void DrawDeferredStartupLoadNotice(const AppRuntimeState& state) {
    // 進捗の刻みが無い区間(CPUパース+メッシュアップロード)なので、バーではなく待機中ということだけを示す。
    if (!state.deferredStartupLoad.pending) return;
    ImGui::SetNextWindowPos(ImVec2(620, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(260, 80), ImGuiCond_FirstUseEver);
    ImGui::Begin("読込中");
    ImGui::TextUnformatted("プラグインを読み込んでいます...");
    ImGui::End();
}

void DrawFrameUiPanels(FrameLoopState& loopState) {
    AppRuntimeGlobals& globals = loopState.globals;
    AppRuntimeState& state = loopState.state;
    globals.editor.BindSceneRegistry(state.sceneRegistry);
    ViewMode& viewMode = loopState.viewMode;

    DrawDeferredStartupLoadNotice(state);

    // 読込予約〜フレーム分割ジョブ完了までは、編集系パネル全部(プラグイン情報
    // パネル・表示切替パネル・モード別パネル群=Rail/Train/Scenery/GameSettings/
    // SaveData全部・運転台視点HUD)を隠し、Scene切替ホットキーも無効化する
    // (画面には「読込中」表示だけが残る)。
    const bool layoutLoading = state.LayoutAction().HasPendingLoad() || state.sceneRenderer.SyncJobActive();
    if (!layoutLoading) {
        // --- プラグインパネル(PI情報/スイッチ/ログ。エディタ/ビューワー両モード共通)。
        // 鑑賞モードでは表示切り替えパネル以外を全て隠すため、これも隠す。 ---
        // 開いたプラグイン(ビューアー・--openplugin)があるときだけ出す。スロットの中身は毎フレーム引き直す。
        PluginSession* openedSession = FindOpenedPluginSession(state);
        if (GetViewModeTraits(viewMode).pluginInfo && openedSession != nullptr) {
            ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(420, 400), ImGuiCond_FirstUseEver);
            PluginInfoAction pluginInfoAction(*openedSession);
            SceneEntitySwitchAction sceneEntitySwitchAction(
                globals.editor.HasSelectedSceneEntity() ? globals.editor.selectedSceneEntityId
                                                        : rs::world::NilGuid(),
                state.sceneRegistry, state.sceneRenderer, &globals.editor);
            DrawPluginInfoPanel({pluginInfoAction, sceneEntitySwitchAction, state.loadLog});
        }

        // 鑑賞中にビューポートのクリックで開いた個体のスイッチ一覧(RmlUi画面のプラグイン設定
        // パネルに相当)。開く・閉じる条件はAppRuntimeFrameRender.cppの設定で決まる。
        if (!state.cli.rmluiWorkspace && !state.viewerMode && !rs::world::GuidIsNil(loopState.imguiPluginSettingsTarget)) {
            SceneEntitySwitchAction settingsAction(loopState.imguiPluginSettingsTarget, state.sceneRegistry,
                                                   state.sceneRenderer, &globals.editor);
            if (DrawSceneEntitySwitchWindow({settingsAction})) loopState.imguiPluginSettingsTarget = rs::world::NilGuid();
        }

        if (state.viewerMode) {
            // プラグインビューアーモードは、表示切り替え・
            // モード別パネル・運転台HUDを出さず、「ビューアー設定」ウィンドウだけを足す。
            // 読込が終わってセッションが引けるまでは出さない。
            PluginSession* viewerSession = FindOpenedPluginSession(state);
            if (viewerSession != nullptr) {
                ViewerSettingsAction viewerSettingsAction(*viewerSession, state.viewer);
                DrawViewerSettingsPanel({viewerSettingsAction});
            }
        } else {
            // --- 表示切り替えパネル(常駐)。レール/列車/情景/ゲーム設定を排他表示する。 ---
            ImGui::SetNextWindowPos(ImVec2(440, 10), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(180, 130), ImGuiCond_FirstUseEver);
            DrawViewModeSwitchPanel(viewMode);

            // 選択中グループのウィンドウのみ表示する。エディタ/列車配置/プラグイン選択系
            // パネルはエディタモード限定(ビューワーモードではゲーム設定のみ選択可)。
            state.modePanels.Draw(viewMode,
                                  {globals.app, globals.camera, state.viewerMode,
                                   state.rmlUiIntegration.WorkspaceView() != nullptr,
                                   globals.editor, globals.lastArcPreview,
                                   globals.editor.graph, state.pluginSelector,
                                   state.sceneRegistry,
                                   state.sceneRenderer, state.loadLog, {globals.gameClock, globals.solarOverrides},
                                   state.localization, state.userSettings, globals.importedWireSpans,
                                   state.layoutSession, state.LayoutAction(), state.layoutDocument,
                                   globals.cabinCamera, state.audioEngine, state.mainWindow.Handle()});
        }
    }
    // 読込ジョブ進捗オーバーレイはここでは描かない。SyncAndTickScene(Sync()呼び出し)
    // より後、AppRuntime.cppのRunFrameLoop内で描く(LoadProgressOverlay.hコメント
    // 参照。Sync()より前だと最初にジョブが始まったフレームで1フレーム古い状態を
    // 見てオーバーレイが出ない)。
}

void HandleSceneSwitchHotkey(FrameLoopState& loopState) {
    AppRuntimeState& state = loopState.state;
    // テキスト入力中(UIのキーボード捕捉中)は、入力Action層が発火させない(WASD移動と同じ規約)。
    // Triggeredは押下エッジのみで、押しっぱなしでは連続発火しない(キーリピートで発火させると
    // 1回のホールドでScene・カメラが高速に何度も切り替わってしまうため)。
    const bool scenePrev = loopState.input.Triggered(rs::app::input::InputAction::ViewScenePrev);
    const bool sceneNext = loopState.input.Triggered(rs::app::input::InputAction::ViewSceneNext);
    if (!scenePrev && !sceneNext) return;
    if (rmlui::RmlUiWorkspaceView* workspace = state.rmlUiIntegration.WorkspaceView()) {
        if (workspace->IsModalOverlayVisible() || !workspace->Snapshot().bottomBarVisible) return;
        if (scenePrev) scene_action::PreviousScene(state.LayoutAction(), workspace->Toasts());
        else scene_action::NextScene(state.LayoutAction(), workspace->Toasts());
    } else {
        state.LayoutAction().SwitchScene(scenePrev ? -1 : +1);
    }
}

void HandleSceneRestoreHotkey(FrameLoopState& loopState) {
    AppRuntimeState& state = loopState.state;
    if (!loopState.input.TriggeredInAnyContext(rs::app::input::InputAction::EditorUndo)) return;
    if (loopState.globals.editor.CanUndo() || !state.LayoutAction().HasRestorableScene()) return;
    if (rmlui::RmlUiWorkspaceView* workspace = state.rmlUiIntegration.WorkspaceView()) {
        if (workspace->IsModalOverlayVisible()) return;
        scene_action::RestoreDeletedScene(state.LayoutAction(), workspace->Toasts(), state.localization);
    } else {
        state.LayoutAction().RestoreDeletedScene();
    }
}

void HandleSceneHotkeys(FrameLoopState& loopState) {
    HandleSceneSwitchHotkey(loopState);
    HandleSceneRestoreHotkey(loopState);
}

} // namespace rs::app

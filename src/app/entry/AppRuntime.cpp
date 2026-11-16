//
// AppRuntime.cpp
//
#include "AppRuntime.h"
#include "AppRuntimeSettingsInit.h"
#include "AppRuntimeFrameInternal.h"
#include "FramePacer.h"
#include "GpuInitFailureDialog.h"
#include "LayoutResultReporter.h"
#include "RenderTuningHotReload.h"
#include "UpdateNotification.h"
#include "ViewerMode.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string_view>

#include <spdlog/spdlog.h>

#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"

#include "../../platform/text/Encoding.h"
#include "../gpu/AppInit.h"
#include "../../render/postprocess/TonemapMath.h"
#include "../input/ImGuiInputBridge.h"
#include "../action/EditToolScope.h"
#include "../action/RenderSettingsAction.h"
#include "../ui/LoadProgressOverlay.h"

namespace rs::app {

bool InitializeAppRuntime(AppRuntimeGlobals& globals, const AppCommandLine& cli, bool viewerMode,
                          const std::string& openPluginPath,
                          AppRuntimeState& state) {
    state.cli = cli;
    state.viewerMode = viewerMode;

    InitializeSettingsAndAutoRecovery(state);

    // 起動フェーズ別所要(ウィンドウが出るまでの体感時間の内訳)。
    using StartupClock = std::chrono::steady_clock;
    const StartupClock::time_point tStartup0 = StartupClock::now();
    auto sinceMs = [](StartupClock::time_point t) {
        return std::chrono::duration<double, std::milli>(StartupClock::now() - t).count();
    };

    // ウィンドウ生成(Win32メッセージポンプ・カメラ入力の委譲はMainWindowへ集約)。
    if (!state.mainWindow.Create(globals.camera, viewerMode, 1280, 720)) return false;
    HWND hwnd = state.mainWindow.Handle();
    const double windowMs = sinceMs(tStartup0);

    // D3D12デバイス+ImGui(Win32/DX12バックエンド)+日本語フォント初期化。
    const StartupClock::time_point tGfx0 = StartupClock::now();
    if (!InitGraphicsAndImGui(globals.app, hwnd, state.mainWindow.Width(), state.mainWindow.Height(),
                              globals.fontRanges, viewerMode)) {
        if (state.cli.smokeFrames == 0) ShowGpuInitFailureDialog(hwnd, state.localization, globals.app.Gpu().InitFailure());
        return false;
    }
    // プラグインビューワーモードは単体プラグイン表示のみで空背景が不要なため、
    // 空(SkyPass)を無効化する。
    globals.app.SetSkyEnabled(!viewerMode);
    globals.app.SetShadowsAllowed(!viewerMode);
    ApplyRenderSettings(globals.app, state.userSettings);
    ApplyRuntimeSettingOverrides(globals.app, state.userSettings, state.cli.settingOverrides);
    state.mainWindow.SetDeviceReady(&globals.app); // 以降 WM_SIZE で g_app.OnResize() を呼ぶ
    state.mainWindow.SetInputSource(&state.input);
    state.mainWindow.SetCabinViewCamera(&globals.cabinCamera);
    const double gfxMs = sinceMs(tGfx0);

    // RmlUi + DX12統合。読み込む文書の種類はRmlUiDocumentKindFor(AppCommandLine.h)が決める。
    if (const std::optional<rs::app::rmlui::RmlUiDocumentKind> kind = RmlUiDocumentKindFor(state.cli)) {
        if (!state.rmlUiIntegration.Init(globals.app.Gpu(), hwnd, state.mainWindow.Width(),
                                          state.mainWindow.Height(), *kind, state.userSettings.uiTheme)) {
            spdlog::error("[rmlui] RmlUiIntegration::Init()に失敗しました(RmlUiの画面を無視します)");
        } else {
            globals.app.SetRmlUiIntegration(&state.rmlUiIntegration);
            state.mainWindow.SetRmlUiIntegration(&state.rmlUiIntegration);
            state.rmlUiIntegration.SetUiSound(&state.uiSound);
            if (rs::app::rmlui::RmlUiViewerView* const viewerView = state.rmlUiIntegration.ViewerView()) {
                viewerView->Attach(state.sceneRegistry, state.sceneRenderer, globals.editor, state.viewer,
                                   [&state]() { return FindOpenedPluginSession(state); });
            }
        }
    }

    // サウンド基盤:XAudio2を初期化する(BVE5互換列車音等の本実装は後続)。
    const StartupClock::time_point tAudio0 = StartupClock::now();
    state.audioEngine.SetOutputDeviceName(state.userSettings.audioOutputDeviceName);
    state.audioEngine.SetUserMasterVolume(state.userSettings.audioMasterVolume);
    state.audioEngine.SetUserTrainVolume(state.userSettings.audioTrainVolume);
    state.audioEngine.SetUserEnvironmentVolume(state.userSettings.audioEnvironmentVolume);
    state.audioEngine.SetUserEffectsVolume(state.userSettings.audioEffectsVolume);
    state.audioEngine.Init();
    state.uiSound.Load(state.audioEngine);
    const double audioMs = sinceMs(tAudio0);

    // 起動時のCLI指定読込(--openplugin/--scene-instance-smoke/--scene-residency-smoke)。
    // 診断系CLIはフレーム0時点で読込済みであることを前提に検証するため
    // ここで同期実行する。それ以外(通常起動・プラグインビューワー)はウィンドウを
    // 先に出すためフレームループへ遅らせる。
    const bool diagnosticsNeedPreloadedContent =
        !state.cli.sceneInstanceSmokePath.empty() ||
        !state.cli.sceneInstanceSmokeBatchSpec.empty() ||
        !state.cli.trainInstanceSmokePath.empty() ||
        !state.cli.sceneResidencySmokePath.empty() || !state.cli.capturePngPath.empty();
    const bool hasStartupLoad =
        !openPluginPath.empty() || !state.cli.openLayoutPath.empty();

    const StartupClock::time_point tAssets0 = StartupClock::now();
    if (hasStartupLoad && !diagnosticsNeedPreloadedContent) {
        state.deferredStartupLoad.pending = true;
        state.deferredStartupLoad.openPluginPath = openPluginPath;
    } else {
        RunStartupLoad(globals, state, openPluginPath);
    }
    const double assetsMs = sinceMs(tAssets0);

    // GameClock: 緯度・地軸傾斜・初期日時のプリセットを実行ファイル埋め込みから読み、
    // 読めなければヘッダ既定値(EnvironmentPreset{}相当)のまま進める。
    // (--openlayoutで.rs2のTimeブロックが見つかればこの後で上書きされる)
    const StartupClock::time_point tPresets0 = StartupClock::now();
    state.environmentPresets.LoadEmbedded();
    state.activePreset = &state.environmentPresets.Default();
    globals.gameClock.SetDateTime(rs::world::FromDateParts(state.activePreset->initialDate),
                                  state.activePreset->initialSecondsOfDay);
    // ビューアーモードは起動時の既定の日時のまま時計を止める(夜の月の位置と満ち欠けを毎回同じにするため)。
    if (viewerMode) globals.gameClock.SetTimeScale(0.0);

    const double presetsMs = sinceMs(tPresets0);
    // .rs2読込は遅延していなければ(診断系CLI経路)ここで同期実行する。遅延時は
    // フレームループ側でプラグイン読込に続けて実行する。
    const StartupClock::time_point tRs20 = StartupClock::now();
    // 参照束の組み立てはここ1箇所だけ。
    state.layoutAction.emplace(LayoutPersistenceRefs{globals.editor, state.pluginSelector,
                                                     state.sceneRegistry, globals.importedWireSpans,
                                                     globals.camera, state.layoutSession,
                                                     state.modePanels.TrainPlacementUi(),
                                                     state.sceneRenderer, globals.gameClock,
                                                     globals.solarOverrides, state.userSettings,
                                                     state.layoutDocument, state.autoRecoveryRoot, state.modePanels.WarpAuthoring(), globals.cabinCamera});

    // 復元の画面を出すときは、選ぶまでレイアウトを開かない(開くレイアウトが異常終了の原因かもしれないため)。
    if (!state.recoveryChoicePending) {
        if (!state.cli.openLayoutPath.empty()) OpenStartupLayout(state, diagnosticsNeedPreloadedContent);
        // 何も開かない通常起動は、新規レイアウトと同じく既定のプラグイン(Surface等)を原点へ置く。
        // ビューアーモードは単体表示なので置かない。
        if (!hasStartupLoad && !viewerMode) state.LayoutAction().NewLayout();
    }
    const double rs2Ms = sinceMs(tRs20);

    ApplySavedWindowSettings(state);
    ShowWindow(hwnd, SW_SHOW);
    StartUpdateCheck(state);
    // 起動に時間がかかる間にほかのウィンドウへフォーカスが移っていても、起動したウィンドウを最前面へ出す。
    SetForegroundWindow(hwnd);
    UpdateWindow(hwnd);
    spdlog::debug("[startup] ウィンドウ表示まで={:.0f}ms window={:.0f}ms gfx+imgui={:.0f}ms "
                 "audio={:.0f}ms assets={:.0f}ms presets={:.0f}ms rs2={:.0f}ms{}",
                 sinceMs(tStartup0), windowMs, gfxMs, audioMs, assetsMs, presetsMs, rs2Ms,
                 state.deferredStartupLoad.pending ? " (読込はフレームループへ遅延)" : "");
    return true;
}

namespace {

// フレームループ1イテレーション分の処理。ウィンドウのリサイズ/移動中はWin32の
// モーダルループ(DispatchMessageの中)がメインループを止めてしまうため、
// MainWindowからも(WM_SIZE/16msタイマー経由で)この関数を直接呼べるように
// 独立させている(呼ばないとドラッグ中はDWMが最後のフレームを引き伸ばして
// 見せるだけになる)。
void RunOneFrame(AppRuntimeGlobals& globals, AppRuntimeState& state, FrameLoopState& loopState,
                 bool& smokeArtifactsWritten) {
    // switchFlipSmokeActiveはstate.cliだけから決まる値なので、ループ本体から
    // 切り出すにあたりここで再計算する(FrameLoopState構築時の値と常に一致する)。
    const bool switchFlipSmokeActive = state.cli.switchFlipSmokeFrames > 0;

    // 1フレームで死ぬ作業用の状態。毎イテレーション新しいローカルとして構築する
    // (寿命がFrameLoopStateと異なるため混ぜない)。
    if (state.deviceLostExit) return; // 終了の手順は済んでいる。ウィンドウが閉じるまで何も描かない
    FrameScratch scratch;
    PollUpdateCheck(state);
    // フレームループ1イテレーション全体の所要時間(ms)。Present/GPU待ちの
    // ブロック時間も含む(断続的なvblank落ちの切り分け用、常時計測)。
    scratch.tCpuFrame0 = std::chrono::steady_clock::now();

#ifdef RS_TUNING_HOT_RELOAD
    // 調整値の読み直しはフレーム先頭の1か所だけ(1フレームの途中で値が変わらないように)。
    TickRenderTuningHotReload(state.renderTuningWatcher,
                              std::chrono::duration<double>(scratch.tCpuFrame0.time_since_epoch()).count());
#endif

    RunDeferredStartupLoadIfDue(loopState);
    TickPendingLayoutLoad(loopState);
    // プラグイン読込直後: 稀な漢字も含めてフォントアトラスを再構築する。
    // NewFrame群より前(フレーム外)でのみ Fonts->Clear/Build を行うこと。
    RunPluginTickAndFontRebuild(loopState);

    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    rs::app::input::PumpImGuiInput(state.input, ImGui::GetIO().DeltaTime);

    if (UsesRmlUi(state.cli)) {
        if (state.input.Triggered(rs::app::input::InputAction::ToggleUi)) state.rmlUiIntegration.ToggleUiHidden();
        state.rmlUiIntegration.NewFrame(ImGui::GetIO().DeltaTime);
        // 捕捉フラグは、PumpImGuiInputの上書きの後でOR合成する(先だとRmlUi側の捕捉が消える)
        state.input.SetUiCapture(state.input.PointerCapturedByUi() || state.rmlUiIntegration.IsPointerOverUi(),
                                 state.input.KeyboardCapturedByUi() || state.rmlUiIntegration.HasTextInputFocus() ||
                                     state.rmlUiIntegration.IsModalOverlayVisible() ||
                                     state.rmlUiIntegration.IsContextMenuOpen() ||
                                     state.rmlUiIntegration.IsConfirmDialogOpen());
        // プラグインビューアーの2枚のウィンドウへ、開いているプラグインの値を渡す。
        if (rmlui::RmlUiViewerView* const viewerView = state.rmlUiIntegration.ViewerView()) {
            viewerView->Refresh(state.openedPlugin.entityId);
        }
        // ワークスペース(--rmlui-workspace)の画面状態と編集ツールをつなぐ。Escの受け持ちは
        // ここ(WorkspaceBack)の1か所で、編集ツール側はEditorCancelを読まない
        // (EditorViewportContext::escapeHandledByWorkspace)。処理順は
        // WorkspaceEditorBridge::Tick参照(UpdateCameraAndEditorより前に終わらせる)。
        if (state.cli.rmluiWorkspace) {
            const GameSettingsRefs gameSettingsRefs{globals.app,
                                                    globals.camera,
                                                    {globals.gameClock, globals.solarOverrides},
                                                    state.localization,
                                                    state.userSettings,
                                                    state.audioEngine,
                                                    state.layoutDocument,
                                                    &state.updateChecker, &state.mainWindow, &state.input};
            const RenderSettingsRefs renderSettingsRefs{globals.app, globals.camera, state.userSettings};
            const WorkspaceBridgeServices bridgeServices{state.pluginSelector, state.userSettings,
                                                         *loopState.pluginBrowser,
                                                         state.sceneRegistry, state.sceneRenderer,
                                                         state.mainWindow.Handle(),
                                                         state.modePanels.SceneryPlacementUi().yawStepDeg,
                                                         state.modePanels.SceneryPlacementUi().continuousPlacement,
                                                         state.modePanels.SceneryPlacementUi().snapToRail,
                                                         state.modePanels.TrainPlacementUi(),
                                                         state.layoutDocument, globals.cabinCamera, globals.camera, state.layoutSession,
                                                         state.modePanels.WarpAuthoring(),
                                                         globals.importedWireSpans, state.localization,
                                                         gameSettingsRefs, renderSettingsRefs, state.LayoutAction()};
            loopState.workspaceBridge.Tick(state.rmlUiIntegration, state.input, globals.editor,
                                           globals.lastArcPreview, loopState.viewMode, bridgeServices,
                                           loopState.pluginSettingsLink, loopState.signalBlockLink,
                                           ImGui::GetIO().DeltaTime,
                                           ImGui::GetIO().Framerate);
            loopState.layoutDocument.Tick({state.rmlUiIntegration, state.LayoutAction(), state.userSettings,
                                           state.mainWindow, state.localization, globals.app});
        }
    }

    // スモークモードでは実測フレーム時間(壁時計)ではなく固定dtへ差し替える。
    // io.DeltaTimeはfixedTickAccumulator(GameClock進行)とプラグイン実行時反映へ
    // そのまま流れるため、実測dtのままだとマシン負荷でtick数・アニメ位相が
    // フレームごとに変動し、PNGキャプチャ等の検証出力が毎回同じにならない。
    if (state.cli.smokeFrames > 0 || switchFlipSmokeActive || loopState.sceneCycleSmoke.Active()) {
        constexpr float kSmokeFixedDeltaSeconds = 1.0f / 60.0f;
        loopState.io.DeltaTime = kSmokeFixedDeltaSeconds;
    }

    // 読込中/進捗ウィンドウ・プラグイン情報パネル・表示切り替えパネル・モード別
    // パネル群の描画。
    // RmlUi画面(確認画面・ワークスペース・ビューアー)はRmlUi側の画面だけを見るモードなので、
    // ImGuiパネルは描かない。ビューアーの起動時読込の待機表示だけはImGuiで出す(進捗オーバーレイと同じ)。
    if (!UsesRmlUi(state.cli)) DrawFrameUiPanels(loopState);
    else if (state.viewerMode) DrawDeferredStartupLoadNotice(state);
    HandleSceneHotkeys(loopState);

    // GameClockの固定tick進行・太陽位置再計算・システムスイッチ更新・
    // ChangeTexture/ChangeMaterial/SetAnimation等の実行時反映。
    AdvanceSimulation(loopState, scratch);

    // 実効の視野角。SyncAndTickScene(LOD・_CAMDIST等)と描画が同じ値を読むよう、その前に1回だけ決める。
    ResolveFrameFieldOfView(loopState, scratch);

    // SceneRegistryとの差分反映(Sync)・switch-flip-smokeの計測/切替・
    // sceneTickループ(列車物理+可動部アニメ)・時間駆動アニメ反映・
    // SubmitTrainRenderSnapshot。
    SyncAndTickScene(loopState, scratch);

    // 読込ジョブ進捗オーバーレイ。
    // 上のSync()呼び出し直後、このフレームの最新のJobActive()状態を見て描く
    // (ImGuiの描画コマンドはApp::Render内のImGui::Render()より前ならどこで
    // 呼んでもよい制約を踏まえた配置。DrawFrameUiPanels側=Sync()より前で
    // 呼ぶと1フレーム古い状態を見てしまう、LoadProgressOverlay.hコメント参照)。
    // RmlUi画面では、同じ表示をLayoutDocumentController::UpdateLoadingDisplayがRmlUiで出す。
    if (!state.cli.rmluiWorkspace) {
        DrawPendingLayoutLoadIndicator(state.LayoutAction(), state.localization);
        DrawLoadProgressOverlay({state.sceneRenderer, state.localization});
    }

    // WASD移動・カメラビュー構築・敷設エディタのTick(ビューワーモードでは無効)。
    UpdateCameraAndEditor(loopState, scratch);
    loopState.syncJobActiveLastFrame = state.sceneRenderer.SyncJobActive();

    // スモーク/PNGキャプチャ判定・App::Render・キャプチャ結果評価・PNG保存・
    // cpuFrameMs算出・FrameProfiler::Record。
    RenderFrameAndCapture(loopState, scratch);

    if (ShouldSkipStartupLoadFrame(loopState)) return;

    ++loopState.frame;
    if (state.cli.smokeFrames > 0 && loopState.frame >= state.cli.smokeFrames) {
        if (!smokeArtifactsWritten) {
            smokeArtifactsWritten = true;
            WriteSmokeArtifacts(globals, state);
        }
        spdlog::info("smoke test: rendered {} frames OK", loopState.frame);
        // 読込や走行だけで未保存の扱いにならないことを、ヘッドレスで確かめるための1行。
        spdlog::info("[layout] dirty={}", state.LayoutAction().DocumentViewModel().dirty);
        PostQuitMessage(0);
    }
    if (switchFlipSmokeActive && loopState.frame >= state.cli.switchFlipSmokeFrames) {
        spdlog::info("[switch-flip-smoke] rendered {} frames OK (切替回数={})", loopState.frame,
                     loopState.switchFlipSmoke.FlipCount());
        PostQuitMessage(0);
    }
}

} // namespace

void RunFrameLoop(AppRuntimeGlobals& globals, AppRuntimeState& state) {
    // どの画面も鑑賞モードから始める。編集用のパネルを隠して3Dビューをほぼ全画面確保する。
    const ViewMode initialViewMode = ViewMode::Spectator;
    // --switch-flip-smoke: 「スイッチ変更→スロット再利用経路」の回帰検証用。60フレーム目
    // 以降、20フレームおきに計5回スロット経路のSceneryエンティティのスイッチ値を変更する
    // (SceneRenderer::Syncパス2のparkedスロット再利用を反復して踏ませる)。この経路は
    // GUIでスイッチを変更したときにしか通らず、通常の--smokeでは一度も実行されないため、
    // ヘッドレスで踏ませる手段が無いとリグレッションを検出できない(GUI自動操作の代わりにCLIで再現する)。
    const bool switchFlipSmokeActive = state.cli.switchFlipSmokeFrames > 0;

    FrameLoopState loopState(globals, state, ImGui::GetIO(), state.input, initialViewMode, switchFlipSmokeActive);
    bool smokeArtifactsWritten = false;

    // ウィンドウのサイズ変更/移動中はWin32のモーダルループがDispatchMessageの中で
    // 回り続け、このwhileループの先頭に戻ってこない(その間DWMは最後のフレームを
    // 引き伸ばして見せるだけになる)。MainWindow側からも1フレーム描けるように
    // コールバックを登録し、ループを抜けたら解除する。
    state.mainWindow.SetModalFrameCallback(
        [&]() { RunOneFrame(globals, state, loopState, smokeArtifactsWritten); });

    // Ctrl+ホイールは、編集が有効な表示モードで高さ調整の動かす先があるときだけ受け取る
    // (敷設ツールが残ったまま他の表示モードにいるときは、ホイールをズームへ回す)。
    state.mainWindow.SetCtrlWheelHandler([&](float notches) {
        if (!IsTrackEditingEnabled(loopState.viewMode, globals.editor)) return false;
        return loopState.placementHeight.AdjustByWheel(notches);
    });

    // ファインダー中の Shift+ホイールは、焦点距離の入力欄を動かす(通常画面の視野角と同じ刻み)。
    state.mainWindow.SetFovWheelHandler([&](float notches) {
        if (!loopState.photoFinderActive) return false;
        rmlui::RmlUiWorkspaceView* const workspaceView = state.rmlUiIntegration.WorkspaceView();
        if (workspaceView == nullptr) return false;
        workspaceView->CameraScreen().StepFocalLength(notches);
        return true;
    });

    // ウィンドウの×は、ワークスペースでは未保存の警告を通す(ImGui版の画面にはその画面が無い)。
    if (state.cli.rmluiWorkspace) {
        state.mainWindow.SetCloseRequestHandler([&]() { loopState.layoutDocument.RequestClose(); });
    }
    if (state.recoveryChoicePending) {
        loopState.layoutDocument.BeginRecoveryChoice(state.autoRecoveryCandidates, state.cli.openLayoutPath);
    }

    FramePacer framePacer;
    while (state.mainWindow.PumpMessages()) {
        RunOneFrame(globals, state, loopState, smokeArtifactsWritten);
        // FPS上限はVSyncオフのときだけ。待ちはフレーム末尾で行い、次フレームのdtには実時間が自然に反映される。
        framePacer.WaitForNextFrame(globals.app.VSyncEnabled() ? 0 : globals.app.FpsLimit());
    }

    state.mainWindow.SetCloseRequestHandler(nullptr);
    state.mainWindow.SetCtrlWheelHandler(nullptr);
    state.mainWindow.SetFovWheelHandler(nullptr);
    state.mainWindow.SetModalFrameCallback(nullptr);
    loopState.frameProfiler.Flush();
}

void ShutdownAppRuntime(AppRuntimeGlobals& globals, AppRuntimeState& state) {
    GetParsedPluginCache().CancelPrefetch();
    if (state.tracksCrashMarker && !state.deviceLostExit && !ClearCrashMarker(state.autoRecoveryRoot)) {
        spdlog::warn("[autorecover] could not clear crash marker");
    }
    state.audioEngine.Shutdown();
    if (UsesRmlUi(state.cli)) {
        // RmlUiIntegration::Shutdown()はGPUがアイドルなことが前提なので、解放の前に完了を待つ。
        globals.app.WaitForGpu();
        globals.app.SetRmlUiIntegration(nullptr);
        state.mainWindow.SetRmlUiIntegration(nullptr);
        state.rmlUiIntegration.Shutdown();
    }
    ShutdownGraphicsAndImGui(globals.app);
    state.mainWindow.Destroy();
    spdlog::info("clean exit");
}

} // namespace rs::app

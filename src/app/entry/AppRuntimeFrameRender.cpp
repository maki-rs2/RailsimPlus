//
// AppRuntimeFrameRender.cpp - RunFrameLoop()の描画関連ステージ
// (カメラ/エディタ/スモークキャプチャ/App::Render/PNG保存/FrameProfiler::Record)
//
#include "AppRuntimeFrameInternal.h"

#include <spdlog/spdlog.h>

#include "DeviceLostShutdown.h"
#include "../diagnostics/BackBufferCapturePng.h"
#include "../diagnostics/LayoutDigest.h"
#include "../diagnostics/SceneInstanceSmokeTest.h"
#include "../diagnostics/SceneResidencySmokeTest.h"
#include "../diagnostics/TrainInstanceSmokeTest.h"
#include "../action/EditToolScope.h"
#include "../action/FormationAction.h"
#include "../action/SceneEntitySwitchAction.h"
#include "../camera/CabinViewCamera.h"
#include "../camera/PhotoCamera.h"
#include "../workspace/WorkModeCatalog.h"
#include "../workspace/ScreenCatalog.h"
#include "../../platform/text/Encoding.h"
#include "../../render/pipeline/EnvFlag.h"
#include "../../sim/TrainRuntime.h"
#include "../../world/alignment/Guid.h"

namespace rs::app {

namespace {

// デバイスロストを検出したあとの終了手順(DeviceLostShutdown.h)。理由とDREDは検出時に GpuContext が出している。
// 読込ジョブ(SyncJobActive)の途中は registry が半端な状態なので、バックアップを書かない
// (書くと、直前の周期的なスナップショットを半端な内容で上書きしてしまう)。
void HandleDeviceLost(FrameLoopState& loopState) {
    AppRuntimeGlobals& globals = loopState.globals;
    AppRuntimeState& state = loopState.state;
    DeviceLostActions actions;
    actions.logReason = [&] {
        spdlog::critical("[device_lost] GetDeviceRemovedReason=0x{:08X}。未保存のレイアウトを保存して終了します",
                         static_cast<unsigned long>(globals.app.Gpu().DeviceLostReason()));
    };
    actions.shouldSaveBackup = [&] {
        if (!HasUnsavedLayout(globals, state)) return false;
        if (state.sceneRenderer.SyncJobActive()) {
            spdlog::warn("[device_lost] 読込ジョブの途中なので、バックアップを書きません");
            return false;
        }
        return true;
    };
    actions.saveBackup = [&] {
        const LayoutActionResult result = state.LayoutAction().WriteAutoRecoverySnapshot();
        if (!result.Ok()) spdlog::error("[device_lost] バックアップの保存に失敗: {} ({})", result.detail, result.diagnostic);
        return result.Ok();
    };
    actions.showError = [&](bool backupAttempted, bool backupSaved) {
        // ヘッドレス実行(--smoke)ではモーダルで止まらないよう、ダイアログを出さない。
        if (state.cli.smokeFrames > 0) return;
        const Localization& text = state.localization;
        std::string body = text.Text("app.device_lost.body");
        if (backupAttempted) {
            body += "\n\n" + text.Text(backupSaved ? "app.device_lost.backup_saved" : "app.device_lost.backup_failed");
        }
        MessageBoxW(state.mainWindow.Handle(), rs::platform_text::Utf8ToWide(body).c_str(),
                    rs::platform_text::Utf8ToWide(text.Text("app.device_lost.title")).c_str(), MB_OK | MB_ICONERROR);
    };
    actions.quit = [&] { PostQuitMessage(0); };
    state.deviceLostExit = true;
    RunDeviceLostShutdown(actions);
}

// 開発用: RS_DEBUG_DEVICE_LOST=<フレーム番号> で、そのフレームにデバイスを取り除いてロストの経路を踏む。
void SimulateDeviceLostIfRequested(FrameLoopState& loopState) {
    static const std::optional<float> kFrame = rs::render::ReadEnvFloat("RS_DEBUG_DEVICE_LOST");
    if (kFrame && loopState.frame == static_cast<int>(*kFrame)) loopState.globals.app.Gpu().SimulateDeviceLost();
}

// 運転台視点の毎フレーム更新: Scene間
// ワープ追従・カブピボット解決・CabinPos移動・CameraView組み立てを行う。
// trueを返せば scratch.view はカブ視点で確定済み(呼び出し側は俯瞰カメラの
// 更新/BuildViewを行わないこと。既存の俯瞰カメラ操作は無効化する)。
bool UpdateCabinView(FrameLoopState& loopState, FrameScratch& scratch) {
    AppRuntimeGlobals& globals = loopState.globals;
    AppRuntimeState& state = loopState.state;
    CabinViewCamera& cabinCamera = globals.cabinCamera;
    if (!cabinCamera.active || rs::world::GuidIsNil(cabinCamera.followedTrainId)) return false;

    const rs::world::TrainId trainId = cabinCamera.followedTrainId;
    rs::sim::TrainRuntime& runtime = state.sceneRenderer.TrainRuntime();

    // Scene間ワープ追従: 追従中編成のsceneIndexがアクティブSceneと異なったら、
    // 既存のSwitchLayoutScene(Ctrl+上下キーと同じ経路)をエッジトリガーで呼ぶ。
    const std::optional<size_t> trainSceneIndex = runtime.GroupSceneIndex(trainId);
    if (trainSceneIndex.has_value() && *trainSceneIndex != state.layoutSession.activeIndex) {
        const int delta =
            static_cast<int>(*trainSceneIndex) - static_cast<int>(state.layoutSession.activeIndex);
        state.LayoutAction().SwitchScene(delta);
    }

    const bool travelReversed = runtime.GroupTravelReversed(trainId);
    bool tailCabin = false;
    rs::core::Mat4 pivotWorld = rs::core::Mat4::Identity();
    const bool resolved = state.sceneRenderer.ResolveCabinPivot(state.sceneRegistry, trainId,
                                                                travelReversed, tailCabin, pivotWorld);
    cabinCamera.followTailCabin = tailCabin;
    cabinCamera.CachePivotIfResolved(resolved, pivotWorld);

    cabinCamera.UpdateMovement(loopState.io.DeltaTime,
                               GetForegroundWindow() == state.mainWindow.Handle(), loopState.input);

    if (!cabinCamera.HasCachedPivot()) return false;
    // FOVは、そのフレームの実効の視野角(通常は俯瞰カメラのユーザー設定値)を使う。
    scratch.view = cabinCamera.BuildView(cabinCamera.CachedPivot(), globals.camera.renderDistance,
                                         scratch.fieldOfView);
    return true;
}

} // namespace

void UpdateCameraAndEditor(FrameLoopState& loopState, FrameScratch& scratch) {
    AppRuntimeGlobals& globals = loopState.globals;
    AppRuntimeState& state = loopState.state;
    ImGuiIO& io = loopState.io;

    const ViewModeTraits& traits = GetViewModeTraits(loopState.viewMode);
    state.input.SetContextActive(input::InputContext::Edit, traits.editContext);
    state.input.SetContextActive(input::InputContext::View, traits.cameraOperations);
    state.input.SetContextActive(input::InputContext::Drive, globals.cabinCamera.active);

    if (UpdateCabinView(loopState, scratch)) {
        // カブ視点が確定した(俯瞰カメラのWASD/右ドラッグ更新は行わない)。
    } else {
        // --- ビューモード: WASDで基準点(target)を水平移動(常時有効、修飾キー・ボタン不要) ---
        // テキスト入力中等(UIのキーボード捕捉中)はショートカットと衝突しないよう無効化する。
        // 地面判定は行わない(target.yはWASDでは変化しない。地面貫通OK)。
        globals.camera.UpdateWasd(io.DeltaTime, GetForegroundWindow() == state.mainWindow.Handle(), loopState.input);
        if (rmlui::RmlUiWorkspaceView* const view = state.rmlUiIntegration.WorkspaceView();
            view != nullptr && workspace::GetScreenLayout(view->Snapshot().screen).hasCameraScreen) {
            globals.camera.UpdateRoll(io.DeltaTime, GetForegroundWindow() == state.mainWindow.Handle(), loopState.input);
        }

        if (!state.cli.cameraOverride.empty()) {
            ApplyCameraOverride(state.cli.cameraOverride, globals.camera.fly, loopState.cameraOverride);
            globals.camera.SnapToTarget();
        }

        state.sceneRenderer.UpdateCameraFollow(state.sceneRegistry, globals.camera); // 追跡と範囲の押し戻し
        globals.camera.Ease(io.DeltaTime);

        // カメラモードに応じて描画用ビューを組み立てる。
        scratch.view = globals.camera.BuildView();
    }
    // 編成情報カードと閉塞ラベルの位置は、カメラが確定したここで求めて次のRmlUiの更新へ渡す
    // (RmlUiのContext::UpdateはTickより前に走るため、Tickで前フレームのカメラを使うより1フレーム新しい)。
    if (state.cli.rmluiWorkspace) {
        const ViewProjResult viewProj = BuildViewProj(scratch.view, state.mainWindow.Width(), state.mainWindow.Height());
        const rs::core::Vec3 eye(scratch.view.eye[0], scratch.view.eye[1], scratch.view.eye[2]);
        loopState.workspaceBridge.UpdateTrainInfoCards(state.rmlUiIntegration, viewProj.viewProj, eye,
                                                       static_cast<float>(state.mainWindow.Width()),
                                                       static_cast<float>(state.mainWindow.Height()));
    }
    // --- 敷設エディタ入力処理・オーバーレイ描画・メッシュ再構築 ---
    // (ビューワーモードではエディタ操作を丸ごと無効化する。読込ジョブ進行中も同様に
    // 無効化する: 編集操作・Undoがregistry/graphを変更しうるため)。
    if (!state.viewerMode && !state.sceneRenderer.SyncJobActive()) {
        const FormationRefs formationRefs{state.pluginSelector, state.sceneRegistry, state.sceneRenderer,
                                          state.modePanels.TrainPlacementUi(), state.layoutDocument,
                                          globals.cabinCamera, globals.camera};
        FormationAction formationAction(formationRefs);
        EditorViewportContext editorCtx{
            globals.editor, state.modePanels.ViewportState(), scratch.view, globals.camera, loopState.input, state.mainWindow.Width(),
            state.mainWindow.Height(), globals.lastArcPreview,
            globals.editorMeshRevision,
            state.pluginSelector, state.sceneRegistry, state.sceneRenderer, globals.app,
            globals.trackPluginEntryRevision, globals.sceneRegistrySceneryRevision,
            globals.sceneRegistryStructureRevision, state.loadLog,
            globals.trackPluginPlacementCache,
            globals.wireEditCache,
            globals.importedWireSpans,
            state.layoutSession,
            state.modePanels.TrainPlacementUi(),
            state.modePanels.SceneryPlacementUi(),
            formationAction,
            loopState.viewMode,
            state.cli.rmluiWorkspace,
            &loopState.pluginSettingsLink,
            &loopState.signalBlockLink,
        };
        // ↑↓キーで敷設・施設配置の高さを動かす(ImGui/RmlUiどちらの画面でも通る位置。
        // TickEditorViewportがplaceHeightを合成する前に反映する)。カブ視点ONの間は
        // 同じキーが運転台の上下移動なので渡さない。
        const bool heightKeysEnabled =
            IsTrackEditingEnabled(loopState.viewMode, globals.editor) && !loopState.input.ContextActive(input::InputContext::Drive);
        loopState.placementHeight.UpdateKeys(
            heightKeysEnabled && loopState.input.Held(rs::app::input::InputAction::EditorHeightUp),
            heightKeysEnabled && loopState.input.Held(rs::app::input::InputAction::EditorHeightDown),
            io.DeltaTime);
        // 情景配置プレビュー中は左右矢印で向きを回す(刻みはUI状態のyawStepDeg)。
        SceneryPlacementUiState& sceneryUi = state.modePanels.SceneryPlacementUi();
        loopState.sceneryYaw.UpdateKeys(
            globals.editor.sceneryPlacementMode && !loopState.input.ContextActive(input::InputContext::Drive),
            loopState.input.Held(rs::app::input::InputAction::SceneryYawLeft),
            loopState.input.Held(rs::app::input::InputAction::SceneryYawRight), io.DeltaTime,
            sceneryUi.yawStepDeg, sceneryUi.yawDeg);
        // ImGui画面でも、RmlUi画面と同じ判定(鑑賞だけ)でクリックから設定ウィンドウを開き、赤い枠を出す。
        // 開いている対象はエディタの選択とは別に持つ。RmlUi画面の値はWorkspaceEditorBridge::Tickが
        // 入れるので、ここでは触らない。
        const bool imguiLink = !state.cli.rmluiWorkspace;
        if (imguiLink) {
            PluginSettingsViewportLink& link = loopState.pluginSettingsLink;
            link.clickEnabled = workspace::ClickOpensPluginSettings(loopState.viewMode);
            // クリックで開けない表示モードへ移ったら閉じる。対象の個体が削除・Undo・Scene切替で
            // なくなったときも閉じる。
            if (!link.clickEnabled ||
                !SceneEntitySwitchAction::TargetExists(loopState.imguiPluginSettingsTarget, state.sceneRegistry,
                                                       state.sceneRenderer)) {
                loopState.imguiPluginSettingsTarget = rs::world::NilGuid();
            }
            link.targetEntityId = loopState.imguiPluginSettingsTarget;
        }
        TickEditorViewport(editorCtx);
        if (imguiLink && !rs::world::GuidIsNil(loopState.pluginSettingsLink.clickedEntityId)) {
            loopState.imguiPluginSettingsTarget = loopState.pluginSettingsLink.clickedEntityId;
            loopState.pluginSettingsLink.clickedEntityId = rs::world::NilGuid();
        } else if (imguiLink && loopState.pluginSettingsLink.clickedEmpty) {
            // プラグインのない場所のクリックでウィンドウを閉じる。
            loopState.imguiPluginSettingsTarget = rs::world::NilGuid();
        }
        // RmlUi画面の合図は、次のフレームのWorkspaceEditorBridge::Tickが読んで消す(ここで消すと届かない)。
        if (imguiLink) loopState.pluginSettingsLink.clickedEmpty = false;
    }
}

void RenderFrameAndCapture(FrameLoopState& loopState, FrameScratch& scratch) {
    AppRuntimeGlobals& globals = loopState.globals;
    AppRuntimeState& state = loopState.state;
    ImGuiIO& io = loopState.io;

    const bool anySmokeReady = state.smokeReady.instanceSmoke || state.smokeReady.residencySmoke ||
                               state.smokeReady.trainInstanceSmoke;
    if (anySmokeReady && loopState.frame == 0) globals.app.RequestBackBufferCapture();
    // --capture-png: --smoke Nの最終フレームでバックバッファをPNG保存する
    // (静的レンダリング目視確認をサブエージェントへ委譲するための経路、
    // GUI自動操作/画像解析は原則メインでは行わない)。
    const bool captureThisFrame = !state.cli.capturePngPath.empty() && state.cli.smokeFrames > 0 &&
                                  loopState.frame == state.cli.smokeFrames - 1;
    if (captureThisFrame) globals.app.RequestBackBufferCapture();
    const bool diagnosticCapture = captureThisFrame || (anySmokeReady && loopState.frame == 0);
    // 保存のサムネイル。ほかの読み戻しと同じフレームなら次のフレームまで待つ。
    loopState.layoutDocument.BeforeRender(globals.app, diagnosticCapture);
    // F12スクリーンショット。ほかの読み戻しと同じフレームなら見送る。
    const bool screenshotRequested = loopState.screenshot.RequestIfTriggered(loopState.input, globals.app,
                                                diagnosticCapture || loopState.layoutDocument.SaveCaptureRequested());
    if (screenshotRequested) {
        state.uiSound.Push(UiSound::Shutter);
    }
    rmlui::RmlUiWorkspaceView* const photoView = state.rmlUiIntegration.WorkspaceView();
    if (photoView != nullptr) {
        loopState.cameraPhoto.BeforeRender(globals.app, photoView->CameraScreen(),
            diagnosticCapture || loopState.layoutDocument.SaveCaptureRequested() || screenshotRequested);
        loopState.cameraFocus.BeforeRender(globals.app, photoView->CameraScreen());
    }
    // 読込ジョブ進行中(SyncJobActive)は3D描画をスキップしUIだけ描く(半端なスロットを描かない)。
    SimulateDeviceLostIfRequested(loopState);
    // カメラ画面では、3Dをファインダーの枠の縦横比で描いて、枠の中へ縮めて出す。
    const PhotoRenderInputs photo = ResolvePhotoRender(loopState, scratch);
    globals.app.SetCameraFinder(photo.finderRect);
    globals.app.SetDepthOfFieldCamera(photo.dofCamera);
    globals.app.SetPhotoExposureMultiplier(photo.exposureMultiplier);
    globals.app.SetLuminanceMeter(photo.luminanceMeter);
    const CameraView& particleView = scratch.view;
    const rs::core::DVec3 particleCamera = {particleView.eye[0], particleView.eye[1], particleView.eye[2]};
    const rs::core::DVec3 particleTarget = {particleView.target[0], particleView.target[1], particleView.target[2]};
    globals.app.SetParticles({state.particleSystem.Pool(), particleCamera,
                              rs::core::Sub(particleTarget, particleCamera), state.particleSystem.Pool().Capacity()},
                             state.userSettings.miscParticle);
    globals.app.Render(scratch.view, state.sceneRenderer.SyncJobActive());
    if (globals.app.DeviceLost()) {
        HandleDeviceLost(loopState);
        return; // 以降はこのデバイスのバックバッファや読み戻しに触れない
    }
    CollectPhotoMeter(loopState);
    loopState.screenshot.SaveIfPending(globals.app);
    if (photoView != nullptr) {
        loopState.cameraPhoto.AfterRender(globals.app, photoView->CameraScreen(), state.localization);
        if (loopState.cameraFocus.AfterRender(globals.app, photoView->CameraScreen())) state.uiSound.Push(UiSound::Focus);
    }
    loopState.layoutDocument.AfterRender(globals.app, state.LayoutAction(), state.localization);
    if (anySmokeReady && loopState.frame == 0) {
        if (state.smokeReady.trainInstanceSmoke)
            EvaluateTrainInstanceSmokeCapture(globals.app, state.cli.capturePngPath,
                                                state.loadLog);
        else if (state.smokeReady.instanceSmoke) EvaluateSceneInstanceSmokeCapture(globals.app);
        else EvaluateSceneResidencySmokeCapture(globals.app);
    }
    if (captureThisFrame) {
        std::vector<uint8_t> capPixels;
        UINT capW = 0, capH = 0, capRowPitch = 0;
        if (globals.app.TakeCapturedBackBuffer(capPixels, capW, capH, capRowPitch)) {
            SaveBackBufferPng(state.cli.capturePngPath, capPixels, capW, capH, capRowPitch,
                              state.loadLog);
        } else {
            spdlog::error("[capture-png] バックバッファ取得に失敗しました");
        }
    }

    scratch.cpuFrameMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - scratch.tCpuFrame0)
            .count();
    loopState.frameProfiler.Record(
        io.DeltaTime * 1000.0, scratch.gameTicksThisFrame, scratch.sceneTickMs,
        scratch.trainTickMsThisFrame, scratch.sceneryTickMsThisFrame, scratch.trainSearchMsThisFrame,
        scratch.trainKinematicsMsThisFrame, scratch.carPartWorldMsThisFrame,
        scratch.trainRuntimeMsThisFrame, scratch.trainSubmitMsThisFrame,
        scratch.carBatchMsThisFrame, scratch.carFallbackMsThisFrame,
        scratch.recomputedSlotsThisFrame, scratch.cpuFrameMs, globals.app.LastPresentMs(),
        globals.app.LastFrameWaitMs(), globals.app.LastGpuFrameMs(), globals.app.LastGpuOpaqueMs(),
        globals.app.LastGpuTransparentMs(), globals.app.LastGpuCompositeMs(), globals.app.LastGpuImguiMs(),
        globals.app.LastGpuTonemapMs(), globals.app.LastGpuAtmosphereMs(), globals.app.LastGpuIblMs(),
        globals.app.LastGpuShadowMs(), globals.app.LastGpuBloomMs(), globals.app.LastGpuFxaaMs(),
        globals.app.LastGpuPrepassMs(), globals.app.LastGpuAoMs(), globals.app.LastGpuMotionBlurMs(),
        globals.app.LastGpuDepthOfFieldMs(), globals.app.LastGpuVolumetricFogMs(),
        globals.app.LastCpuRenderBreakdown());
}

void WriteSmokeArtifacts(AppRuntimeGlobals& globals, AppRuntimeState& state) {
    if (!state.cli.saveLayoutPath.empty()) {
        const LayoutActionResult result = state.LayoutAction().SaveRsl(state.cli.saveLayoutPath);
        if (result.Ok()) {
            spdlog::info("[save-layout] saved '{}'", result.savedPathUtf8);
        } else {
            spdlog::error("[save-layout] failed '{}': {}", state.cli.saveLayoutPath, result.detail);
        }
    }
    if (!state.cli.layoutDigestPath.empty()) {
        const LayoutDigestResult result =
            WriteLayoutDigest(state.cli.layoutDigestPath, state.layoutSession, globals.editor, state.sceneRegistry,
                              globals.camera, state.pluginSelector);
        if (result.ok) {
            spdlog::info("[layout-digest] wrote '{}'", state.cli.layoutDigestPath);
        } else {
            spdlog::error("[layout-digest] failed '{}': {}", state.cli.layoutDigestPath, result.detail);
        }
    }
}

bool ShouldSkipStartupLoadFrame(FrameLoopState& loopState) {
    if (loopState.state.deferredStartupLoad.pending) {
        ++loopState.framesBeforeStartupLoad; // 助走フレーム(読込前)はスモークに数えない
        return true;
    }
    // レイアウト読込・Scene切替のフレーム分割ジョブが進行中の間も同様にフレームカウントを進めない。--smoke/--capture-png/
    // --save-layout/--layout-digest/--switch-flip-smokeはいずれもloopState.frameの
    // 増分を起点に完了判定するため、ここ1箇所で止めれば専用の同期ループを
    // 各CLI側に個別実装せずに済む(半端な読込状態を検証対象にしないため)。
    // 読込予約中(インジケータ表示待ち〜実行待ちの間)もフレームカウントを進めない
    // (--smoke等の完了判定を乱さないため、SyncJobActive()と同じ扱い)。
    if (loopState.state.LayoutAction().HasPendingLoad()) return true;
    if (loopState.state.sceneRenderer.SyncJobActive()) return true;
    return false;
}

} // namespace rs::app

//
// WorkspaceEditorBridge.cpp
//
#include "WorkspaceEditorBridge.h"

#include <utility>

#include <RmlUi/Core/ElementDocument.h>

#include "../action/LayoutPersistenceAction.h"
#include "../action/MissingPluginRefreshGate.h"
#include "../action/PluginRootAction.h"
#include "../action/SceneEntitySwitchAction.h"
#include "../action/SceneryPlacementAction.h"
#include "../action/FormationAction.h"
#include "../camera/CabinViewCamera.h"
#include "../action/SceneSignalAspectSource.h"
#include "../action/SceneSignalSidecarSource.h"
#include "../action/SignalBlockAction.h"
#include "../action/SignalPanelAction.h"
#include "../action/SignalPanelViewModel.h"
#include "../action/ToolCancelAction.h"
#include "../action/TrackLayingAction.h"
#include "../plugin_browser/PluginBrowserService.h"
#include "../plugin_session/LayoutSession.h"
#include "../rmlui/RmlUiIntegration.h"
#include "../settings/UserSettings.h"
#include "../workspace/WorkModeCatalog.h"
#include "../../editor/tool/Editor.h"
#include "../../world/scene/SceneRegistry.h"

namespace rs::app {

void WorkspaceEditorBridge::Tick(rmlui::RmlUiIntegration& rmlUi, const rs::app::input::InputSource& input,
                                 rs::editor::Editor& editor, const rs::editor::PlacePreview& lastArcPreview,
                                 ViewMode& viewMode, const WorkspaceBridgeServices& services,
                                 PluginSettingsViewportLink& pluginSettingsLink,
                                 const SignalBlockViewportLink& signalBlockLink, double deltaSeconds,
                                 float framesPerSecond) {
    editor.BindSceneRegistry(services.sceneRegistry);
    rmlui::RmlUiWorkspaceView* const view = rmlUi.WorkspaceView();
    if (view == nullptr) return;

    // UIテーマはユーザー設定の値に合わせる(変わっていなければ何もしない)。
    rmlUi.SetUiTheme(services.userSettings.uiTheme);

    // 敷設エディタの表示は、最初のTickでEditorと敷設プレビューを渡してから始める。
    if (!layingEditorAttached_) {
        view->LayingEditor().Attach(editor, lastArcPreview);
        view->EdgeEdit().Attach(editor, services.layoutSession, services.pluginSelector,
                                services.warpAuthoring);
        view->LayingSet().Attach(editor, services.pluginSelector, services.pluginBrowser);
        view->TemplateSave().Attach(editor, services.pluginSelector, PluginRootAction(services.userSettings).Roots());
        SceneRenderer& renderer = services.sceneRenderer;
        view->PlatformCard().Attach([view](rs::world::Guid platformId) { view->DiaPanel().OpenPlatform(platformId); });
        view->RailLabel().Attach(
            [&editor, &renderer](rs::world::Guid guid, bool ensureSelected) {
                if (ensureSelected && editor.selectedBlock == guid) return;
                SceneSignalSidecarSource sidecar(renderer);
                SceneSignalAspectSource aspects(renderer);
                SignalBlockAction(editor, sidecar, aspects).SelectBlock(guid);
            },
            [&editor, &renderer](rs::world::Guid guid, const std::string& name) {
                SceneSignalSidecarSource sidecar(renderer);
                SceneSignalAspectSource aspects(renderer);
                SignalBlockAction(editor, sidecar, aspects).RenameBlock(guid, name);
            },
            [&editor, &renderer, &session = services.layoutSession](rs::world::Guid guid) {
                SceneSignalSidecarSource sidecar(renderer);
                SceneSignalAspectSource aspects(renderer);
                SignalBlockAction(editor, sidecar, aspects).ChooseSpeedSignalOwner(
                    guid, &session.warpLinks, session.activeIndex);
            },
            [&editor] { return editor.CancelSpeedSignalPending(); });
        view->SignalPanel().Attach(
            [&editor, &renderer]() {
                SceneSignalSidecarSource sidecar(renderer);
                SceneSignalAspectSource aspects(renderer);
                SignalBlockAction(editor, sidecar, aspects).CreateBlockFromSelection();
            },
            [&editor, &renderer, &session = services.layoutSession](
                const rmlui::RmlUiSignalPanelView::SignalActionFn& run) {
                SceneSignalSidecarSource sidecar(renderer);
                SceneSignalAspectSource aspects(renderer);
                SignalBlockAction action(editor, sidecar, aspects);
                run(action, &session.warpLinks, session.activeIndex);
            });
        layingEditorAttached_ = true;
    }
    if (!pluginBrowserAttached_) {
        view->PluginBrowser().Attach(services.pluginBrowser, editor, services.pluginSelector, services.userSettings,
                                   services.sceneRenderer, services.sceneRegistry, services.sceneryContinuous,
                                   services.scenerySnapToRail, lastArcPreview, view->Toasts());
        // カードの画像(pbimg:...)は、ブラウザの画像の置き場から直接レンダラへ渡す。
        plugin_browser::PluginBrowserService& browser = services.pluginBrowser;
        // レンダラの引き先は1つなので、レイアウトの一覧のサムネイル(ltimg:...)もここで引く。
        rmlUi.SetTextureSourceResolver([&browser, view](const Rml::String& source, rs::render::ImageData& out) {
            return view->LayoutFiles().ResolveImageSource(source, out) || browser.ResolveImageSource(source, out);
        });
        pluginBrowserAttached_ = true;
    }
    if (!pluginRootsAttached_) {
        view->PluginRoots().Attach(services.pluginBrowser, services.userSettings, services.windowHandle);
        view->CameraScreen().AttachSettings(services.userSettings);
        view->CameraScreen().AttachCamera(services.gameSettings.camera);
        // ルートが空のまま起動したら、登録を促すためにルート管理ウィンドウを開く(起動ごとに1回)。
        if (services.userSettings.pluginRoots.empty()) {
            view->PluginRoots().SetFirstRun();
            view->OpenOverlay(workspace::OverlayKind::PluginRoots);
        }
        pluginRootsAttached_ = true;
    }
    if (!missingPluginsAttached_) {
        UserSettingsData& userSettings = services.userSettings;
        const MissingPluginRefs refs{editor,
                                     services.sceneRegistry,
                                     services.pluginSelector,
                                     services.layoutSession,
                                     services.importedWireSpans,
                                     [&userSettings]() { return userSettings.pluginRoots; },
                                     {},
                                     {}};
        view->MissingPlugins().Attach(refs, userSettings, view->Toasts(), services.pluginBrowser, services.localization,
                                      services.windowHandle);
        missingPluginsAttached_ = true;
    }
    if (!gameSettingsAttached_) {
        view->GameSettings().Attach(services.gameSettings, services.windowHandle, rmlUi.ThemeSet());
        view->AttachInput(services.gameSettings.inputSource);
        view->RenderSettings().Attach(services.renderSettings, services.localization);
        view->TimeControl().Attach(services.gameSettings);
        view->ScenePanel().Attach(services.layoutAction, services.localization);
        gameSettingsAttached_ = true;
    }
    if (!sceneryYawStepAttached_) {
        view->SceneryYawStep().Attach(services.sceneryYawStepDeg);
        sceneryYawStepAttached_ = true;
    }
    if (!formationListAttached_) {
        view->FormationList().Attach({services.pluginSelector, services.sceneRegistry, services.sceneRenderer,
                                      services.trainPlacementUi, services.layoutDocument, services.cabinCamera, services.camera},
                                     services.pluginBrowser, view->Toasts());
        view->TrainTab().Attach({services.pluginSelector, services.sceneRegistry, services.sceneRenderer,
                                 services.trainPlacementUi, services.layoutDocument, services.cabinCamera, services.camera});
        view->TrainTab().AttachLaying(editor, services.pluginSelector, PluginRootAction(services.userSettings).Roots());
        view->TrainTab().AttachBrowser(services.pluginBrowser);
        view->TrainInfoCard().Attach({services.pluginSelector, services.sceneRegistry, services.sceneRenderer,
                                      services.trainPlacementUi, services.layoutDocument, services.cabinCamera, services.camera});
        view->CabinHud().Attach({services.pluginSelector, services.sceneRegistry, services.sceneRenderer,
                                 services.trainPlacementUi, services.layoutDocument, services.cabinCamera, services.camera});
        formationListAttached_ = true;
    }
    if (!diaPanelAttached_) {
        view->DiaPanel().Attach(editor, services.layoutSession, services.pluginSelector, services.sceneRenderer,
                                services.camera,
                                [&layoutAction = services.layoutAction](int delta) { layoutAction.SwitchScene(delta); });
        diaPanelAttached_ = true;
    }
    if (!pluginSettingsAttached_) {
        view->PluginSettings().Attach(services.sceneRegistry, services.sceneRenderer, editor);
        pluginSettingsAttached_ = true;
    }

    // ビューポートのクリックで開く。クリックは前のフレームのTickEditorViewportが書いたもので、
    // そのフレームの描画のあとに画面状態へ渡すので、パネルは1フレーム遅れて開く。
    if (!rs::world::GuidIsNil(pluginSettingsLink.clickedEntityId)) {
        view->OpenPluginSettings(pluginSettingsLink.clickedEntityId);
        pluginSettingsLink.clickedEntityId = rs::world::NilGuid();
    } else if (pluginSettingsLink.clickedEmpty) {
        // プラグインのない場所のクリックでパネルを閉じる。
        view->ClosePluginSettings();
    }
    pluginSettingsLink.clickedEmpty = false;
    // 対象の個体が削除・Undo・Scene切替でなくなったらパネルを閉じる。
    const rs::world::Guid& openTarget = view->PluginSettingsTarget();
    if (!rs::world::GuidIsNil(openTarget) &&
        !SceneEntitySwitchAction::TargetExists(openTarget, services.sceneRegistry, services.sceneRenderer)) {
        view->ClosePluginSettings();
    }

    // 運転台視点の間は固定UIを消す(Escの判定より前に知らせる)。
    view->SetCabinView(services.cabinCamera.active && !rs::world::GuidIsNil(services.cabinCamera.followedTrainId));

    if (input.Triggered(rs::app::input::InputAction::WorkspaceBack)) {
        input::EscapeHandlers handlers{};
        handlers[workspace::EnumIndex(input::EscapeTarget::SignalPlacement)] = [&] {
            return editor.CancelSpeedSignalPending();
        };
        handlers[workspace::EnumIndex(input::EscapeTarget::Tool)] = [&] {
            ToolCancelAction toolCancel(editor, viewMode);
            const workspace::BackResult result = view->Back(toolCancel.HasCancelableOperation());
            if (result == workspace::BackResult::DeferredToTool) toolCancel.CancelOperation();
            if (result == workspace::BackResult::LeaveCabinView) {
                FormationAction({services.pluginSelector, services.sceneRegistry, services.sceneRenderer,
                                 services.trainPlacementUi, services.layoutDocument, services.cabinCamera, services.camera})
                    .LeaveCabinView();
                view->SetCabinView(false);
            }
            return true;
        };
        input::DispatchEscape(handlers);
    }

    for (const workspace::WorkModeLayout& mode : workspace::kWorkModeCatalog) {
        if (mode.shortcutAction.has_value() && input.Triggered(*mode.shortcutAction)) view->SelectMode(mode.mode);
    }

    const workspace::WorkspaceSnapshot& snapshot = view->Snapshot();
    const ViewMode previousViewMode = viewMode;
    viewMode = workspace::ToViewMode(snapshot);
    // モードを切り替えたら、情景の配置プレビュー(ブラウザで選んだ情景)を解除する。
    if (viewMode != previousViewMode) SceneryPlacementAction(editor).End();
    const bool sectionEditVisible = snapshot.LayingEditorPresent() &&
                                    snapshot.railTab == workspace::RailTab::Edit;
    editor.sectionEditMode = sectionEditVisible;
    if (!sectionEditVisible) {
        editor.sectionSplitMode = false;
        editor.CancelSectionBoundaryDrag();
    }
    if (snapshot.LayingEditorPresent()) {
        TrackLayingAction(editor, lastArcPreview).SelectTool(snapshot.railTab == workspace::RailTab::Lay);
        // 画面に出ているフレームだけ、敷設エディタの値を更新する(変わった変数だけ反映される)。
        view->LayingEditor().Refresh();
        view->LayingSet().Refresh();
        if (snapshot.railTab == workspace::RailTab::Edit) view->EdgeEdit().Refresh();
    } else {
        view->LayingEditor().ResetInputFocus();
    }

    // プラグインブラウザ: 裏側(走査)は毎フレーム進め、画面の値は左タブがブラウザで開いているフレームだけ更新する。
    const workspace::EdgePanelSnapshot& leftTab = snapshot.Tab(workspace::NotchSide::Left);
    const bool browserVisible = leftTab.open && leftTab.content == workspace::TabContent::PluginBrowser;
    const plugin_browser::BrowserProfile profile =
        snapshot.InWorkMode() ? workspace::GetWorkModeLayout(snapshot.workMode).browserProfile
                              : plugin_browser::BrowserProfile::None;
    // 前のフレームの描画で測ったカード画像の作成時間を渡す(新しく見せる枚数を決めるため)。
    const RenderInterface_DX12::ResolvedTextureLoadStats loadStats = rmlUi.TakeResolvedTextureLoadStats();
    view->PluginBrowser().ObserveTextureLoads(loadStats.totalMs, loadStats.count);
    services.pluginBrowser.Tick(services.userSettings.pluginRoots, profile, browserVisible);
    Rml::ElementDocument* const document = rmlUi.Document();
    if (browserVisible && document != nullptr) view->PluginBrowser().Refresh(*document);
    if (browserVisible) view->SceneryYawStep().Refresh();
    const bool trainProfile = profile == plugin_browser::BrowserProfile::Train;
    const bool signalMode = GetViewModeTraits(viewMode).signalBlock;
    std::optional<rs::app::rmlui::RmlUiSignalPanelView::BlockAnchor> blockAnchor;
    std::vector<workspace::SignalPanelSignal> signalCards;
    if (signalMode) {
        SceneSignalSidecarSource sidecar(services.sceneRenderer);
        SceneSignalAspectSource aspects(services.sceneRenderer);
        const SignalBlockAction signalAction(editor, sidecar, aspects);
        rs::app::rmlui::RmlUiSignalPanelView::BlockAnchor anchor;
        if (signalAction.BlockCreateAnchor(anchor.edge, anchor.s)) blockAnchor = anchor;
        signalCards = BuildSignalPanelSignals(editor.graph, signalAction);
    }
    view->SignalPanel().Refresh(signalMode, editor.graph, blockAnchor, document, std::move(signalCards));
    // 編成リストと編成情報カードは同じ画像を使うので、画像の1フレームの囲みは列車モードの間ここで1回だけ行う。
    rmlui::RmlUiFormationImages& formationImages = view->FormationImages();
    if (trainProfile) formationImages.BeginFrame(services.pluginBrowser);
    view->FormationList().Refresh(browserVisible && trainProfile, trainProfile, formationImages);
    view->TrainTab().Refresh(profile, PluginRootAction(services.userSettings).Roots());
    view->CabinHud().Refresh();
    view->DiaPanel().Tick();
    view->TrainInfoCard().Refresh(trainProfile, services.layoutSession.activeIndex, deltaSeconds, document,
                                  formationImages);
    SceneSignalSidecarSource signalSidecar(services.sceneRenderer);
    SceneSignalAspectSource signalAspects(services.sceneRenderer);
    const SignalBlockViewModel signalPlacement = editor.speedSignalPending
        ? SignalBlockAction(editor, signalSidecar, signalAspects).ViewModel() : SignalBlockViewModel{};
    view->RailLabel().Refresh(GetViewModeTraits(viewMode).signalBlock, editor.graph, signalBlockLink, signalPlacement,
                              editor.selectedBlock, services.layoutSession.activeIndex, deltaSeconds, document);
    view->PlatformCard().Refresh(GetViewModeTraits(viewMode).stationDia, editor.graph, editor.selectedPlatform, document);
    if (trainProfile) {
        formationImages.EndFrame();
    } else {
        formationImages.ReleaseAll();
    }

    // ルート管理ウィンドウ: 見えているフレームと、ダイアログが開いているフレーム(ウィンドウを閉じたあとも
    // 結果を受け取る)だけ更新する。
    if (snapshot.Overlay(workspace::OverlayKind::PluginRoots).visible || view->PluginRoots().DialogOpen()) {
        view->PluginRoots().Refresh();
    }

    // 見つからないプラグイン: 警告バッジはウィンドウの開閉に関係なく出すので、毎フレーム呼ぶ。一覧を作り直すのは
    // 状態の写しが変わったときだけ(MissingPluginRefreshGate)。差し替えのダイアログが開いているフレームも呼ぶ。
    const bool missingWindowVisible = snapshot.Overlay(workspace::OverlayKind::MissingPlugins).visible;
    view->Tick(deltaSeconds);
    view->MissingPlugins().Tick(
        MakeMissingPluginSignature(editor, services.sceneRegistry, services.layoutSession, services.pluginSelector,
                                   services.userSettings.pluginRoots),
        deltaSeconds);
    // 全部解決したら閉じる。
    if (missingWindowVisible && view->MissingPlugins().NothingMissing()) {
        view->CloseOverlay(workspace::OverlayKind::MissingPlugins);
    }

    // ゲーム設定: ダイアログの結果を受け取るため毎フレーム呼ぶ。画面の値は見えているフレームだけ更新される。
    const bool gameSettingsVisible = snapshot.Overlay(workspace::OverlayKind::GameSettings).visible;
    view->GameSettings().Tick(gameSettingsVisible, framesPerSecond);
    view->RenderSettings().Tick(); // 言語の切り替えは、窓が閉じていても文言へ反映する

    // プラグイン設定パネル: 見えているフレームだけスイッチ一覧を更新する(対象が消えて閉じた場合は見えない)。
    if (snapshot.Overlay(workspace::OverlayKind::PluginSettingsPanel).visible) {
        view->PluginSettings().Refresh(view->PluginSettingsTarget());
    }

    // ビューポートへ渡す値。状態を変える処理(Escなど)のあとで入れる。
    pluginSettingsLink.clickEnabled = workspace::ClickOpensPluginSettings(snapshot);
    pluginSettingsLink.targetEntityId = view->PluginSettingsTarget();
}

void WorkspaceEditorBridge::UpdateTrainInfoCards(rmlui::RmlUiIntegration& rmlUi, const rs::core::Mat4& viewProj,
                                                 const rs::core::Vec3& eye, float screenWidth, float screenHeight) {
    rmlui::RmlUiWorkspaceView* const view = rmlUi.WorkspaceView();
    if (view == nullptr) return;
    view->TrainInfoCard().UpdatePositions(viewProj, eye, screenWidth, screenHeight);
    view->RailLabel().UpdatePositions(viewProj, screenWidth, screenHeight);
    view->PlatformCard().UpdatePositions(viewProj, screenWidth, screenHeight);
    view->SignalPanel().UpdatePositions(viewProj, screenWidth, screenHeight);
}

} // namespace rs::app

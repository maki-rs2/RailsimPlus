#include "LayoutDocumentController.h"

#include <filesystem>
#include <format>
#include <utility>

#include "MainWindow.h"
#include "../gpu/App.h"
#include "../action/LayoutPersistenceAction.h"
#include "../plugin_session/LayoutFileCatalog.h"
#include "../plugin_session/LayoutThumbnail.h"
#include "../rmlui/RmlUiConfirmDialog.h"
#include "../rmlui/RmlUiIntegration.h"
#include "../settings/UserSettings.h"
#include "../ui/LoadProgressText.h"
#include "../ui/UiPanels.h"
#include "../workspace/WorkspaceShell.h"
#include "../../platform/text/Encoding.h"

namespace rs::app {

using workspace::LayoutFlowEffect;
using workspace::OverlayKind;
using workspace::UnsavedChoice;

namespace {

bool FileExists(const std::string& pathUtf8) {
    std::error_code error;
    return std::filesystem::exists(std::filesystem::path(rs::platform_text::Utf8ToWide(pathUtf8)), error) && !error;
}

} // namespace

std::string LayoutDocumentController::NameOrUntitled(const std::string& name, const Localization& localization) {
    return name.empty() ? localization.Text("layout.document.untitled") : name;
}

void LayoutDocumentController::Tick(const LayoutDocumentServices& services) {
    rmlui::RmlUiWorkspaceView* const view = services.rmlUi.WorkspaceView();
    if (view == nullptr) return;

    view->LayoutFiles().SetOwnerWindow(services.mainWindow.Handle());
    if (deferredLoad_.has_value()) {
        const DeferredLoad load = *std::exchange(deferredLoad_, std::nullopt);
        if (load.pathUtf8.empty()) {
            // 新規。今のレイアウトの復元データは、識別子が新しいものへ替わる前に消す。
            if (load.discardRecovery) services.action.DiscardCurrentRecovery();
            services.action.NewLayout();
        } else {
            loadFromFilesScreen_ = true;
            loadingPathUtf8_ = load.pathUtf8;
            services.action.RequestLoadLayout(load.pathUtf8, load.discardRecovery);
        }
    }
    if (std::exchange(startAfterRestoreFailure_, false)) services.action.NewLayout();
    TickRecoveryChoice(services);

    const std::string recentToAdd = std::exchange(recentToAddUtf8_, {});
    if (!recentToAdd.empty()) {
        AddRecentLayout(services.userSettings.recentLayouts, recentToAdd);
        SaveUserSettingsOrWarn(services.userSettings, "Failed to save user settings after updating recent layouts.");
    }
    if (std::optional<bool> ok = std::exchange(saveFinished_, std::nullopt)) {
        Apply(flow_.OnSaveFinished(*ok), services);
    }

    if (closeRequested_) {
        closeRequested_ = false;
        Apply(flow_.RequestQuit(services.action.DocumentViewModel().dirty, services.action.DocumentSavedPath()),
              services);
    }
    if (std::optional<UnsavedChoice> answer = std::exchange(unsavedAnswer_, std::nullopt)) {
        Apply(flow_.OnUnsavedChoice(*answer), services);
    }
    if (std::optional<bool> answer = std::exchange(overwriteAnswer_, std::nullopt)) {
        Apply(flow_.OnOverwriteChoice(*answer), services);
    }
    if (std::optional<bool> answer = std::exchange(quitAnswer_, std::nullopt)) {
        Apply(flow_.OnQuitChoice(*answer), services);
    }
    HandleMenuCommand(services);
    HandleFilesCommand(services);

    UpdateLoadingDisplay(services);

    // 名前を待っている間に×やEscで画面が閉じられたら、保存をやめる(続く終了やロードも取りやめる)。
    if (flow_.InSaveAs() && !view->Snapshot().Overlay(OverlayKind::LayoutFiles).visible) {
        Apply(flow_.OnSaveAsCancelled(), services);
    }
    if (!failureMessage_.empty()) {
        if (rmlui::RmlUiConfirmDialog* const dialog = services.rmlUi.ConfirmDialog()) {
            rmlui::ConfirmDialogContent content;
            content.message = std::exchange(failureMessage_, {});
            content.confirmLabel = services.localization.Text("layout.document.close");
            content.cancelLabel.clear();
            dialog->Open(std::move(content));
        }
    }
    if (view->Snapshot().Overlay(OverlayKind::LayoutFiles).visible && services.rmlUi.Document() != nullptr) {
        view->LayoutFiles().Refresh(*services.rmlUi.Document());
    }
}

void LayoutDocumentController::OnLayoutFinished(const FinishedLayoutResult& finished,
                                                const Localization& localization) {
    const LayoutActionResult& result = finished.result;
    // 一覧の画面・復元から始めた読込かの印は、読込の結果だけが消費する(新規の結果が先に来ても残す)。
    const bool isLoad = finished.kind == FinishedLayoutKind::Load;
    const bool restoring = isLoad && std::exchange(restoringRecovery_, false);
    const bool fromFilesScreen = isLoad && std::exchange(loadFromFilesScreen_, false);
    if (result.Ok()) {
        // .rs2 は一覧にいつでも出るので、最近使ったファイルへは入れない。
        if (fromFilesScreen && !IsRs2Path(loadingPathUtf8_)) recentToAddUtf8_ = loadingPathUtf8_;
        return;
    }
    failureMessage_ = LayoutActionErrorMessage(result, localization);
    // 復元の画面はレイアウトを開かずに待っていたので、失敗したら空のレイアウトで始める。
    startAfterRestoreFailure_ = restoring;
}

void LayoutDocumentController::UpdateLoadingDisplay(const LayoutDocumentServices& services) {
    rmlui::RmlUiLoadingView& loading = services.rmlUi.WorkspaceView()->Loading();
    const bool busy = IsBusy(services);
    if (!busy) {
        loading.Set(false, {}, 0.0f, {});
        loadingDisplayed_ = false;
        return;
    }
    // 読込が始まった最初のフレームで鑑賞へ戻す(読み込んだレイアウトは鑑賞から始める)。
    if (!loadingDisplayed_) services.rmlUi.WorkspaceView()->ReturnToSpectate();
    const LayoutLoadProgress progress = services.action.LoadProgress();
    if (progress.jobActive) {
        loading.Set(true, LoadPhaseLabel(progress.phase, services.localization),
                    static_cast<float>(progress.fraction),
                    LoadProgressPercentText(progress.fraction, services.localization));
    } else {
        loading.Set(true, services.localization.Text(kLoadPendingKey), 0.0f, {});
    }
    // 前のTickで出した表示は、このフレームの描画に入っている。
    if (loadingDisplayed_) services.action.MarkPendingLoadIndicatorShown();
    loadingDisplayed_ = true;
}

bool LayoutDocumentController::IsBusy(const LayoutDocumentServices& services) const {
    return deferredLoad_.has_value() || services.action.IsLoading();
}

void LayoutDocumentController::HandleMenuCommand(const LayoutDocumentServices& services) {
    rmlui::RmlUiWorkspaceView& view = *services.rmlUi.WorkspaceView();
    LayoutPersistenceAction& action = services.action;
    const rmlui::MainMenuCommand command = view.TakeMainMenuCommand();
    if (IsBusy(services) && command != rmlui::MainMenuCommand::Quit) return;
    switch (command) {
    case rmlui::MainMenuCommand::None: break;
    case rmlui::MainMenuCommand::Save: Apply(flow_.RequestSave(action.DocumentSavedPath()), services); break;
    case rmlui::MainMenuCommand::SaveAs: Apply(flow_.RequestSaveAs(), services); break;
    case rmlui::MainMenuCommand::Load:
        if (flow_.Idle()) OpenFilesScreen(false, services);
        break;
    case rmlui::MainMenuCommand::Quit: RequestClose(); break;
    }
}

void LayoutDocumentController::HandleFilesCommand(const LayoutDocumentServices& services) {
    rmlui::RmlUiWorkspaceView& view = *services.rmlUi.WorkspaceView();
    LayoutPersistenceAction& action = services.action;
    const std::optional<rmlui::LayoutFilesCommand> command = view.LayoutFiles().TakeCommand();
    if (!command.has_value()) return;
    if (IsBusy(services) && command->kind != rmlui::LayoutFilesCommand::Kind::Close) return;
    switch (command->kind) {
    case rmlui::LayoutFilesCommand::Kind::LoadFile:
        Apply(flow_.RequestLoad(command->value, action.DocumentViewModel().dirty, action.DocumentSavedPath()),
              services);
        break;
    case rmlui::LayoutFilesCommand::Kind::NewLayout:
        // 新規も、未保存の変更を聞いてから切り替える流れはロードと同じなので、空のパスとしてロードに乗せる。
        Apply(flow_.RequestLoad({}, action.DocumentViewModel().dirty, action.DocumentSavedPath()), services);
        break;
    case rmlui::LayoutFilesCommand::Kind::SaveName: {
        const std::string pathUtf8 = action.ResolveSavePath(command->value);
        Apply(flow_.OnSaveAsName(pathUtf8, FileExists(pathUtf8)), services);
        break;
    }
    case rmlui::LayoutFilesCommand::Kind::Close:
        view.CloseOverlay(OverlayKind::LayoutFiles);
        break;
    }
}

void LayoutDocumentController::Apply(const workspace::LayoutFlowStep& step, const LayoutDocumentServices& services) {
    rmlui::RmlUiWorkspaceView& view = *services.rmlUi.WorkspaceView();
    switch (step.effect) {
    case LayoutFlowEffect::None: break;
    case LayoutFlowEffect::ShowUnsavedWarning: ShowUnsavedWarning(services); break;
    case LayoutFlowEffect::OpenSaveAs: OpenFilesScreen(true, services); break;
    case LayoutFlowEffect::ConfirmOverwrite: ShowOverwriteConfirm(step.pathUtf8, services); break;
    case LayoutFlowEffect::ConfirmQuit: ShowQuitConfirm(services); break;
    case LayoutFlowEffect::Save:
        view.CloseOverlay(OverlayKind::LayoutFiles);
        pendingSavePathUtf8_ = step.pathUtf8;
        break;
    case LayoutFlowEffect::Load:
        view.CloseOverlay(OverlayKind::LayoutFiles);
        // 対象は読込前のレイアウトの個体なので、パネルを閉じる。
        view.ClosePluginSettings();
        // 読込の要求は、閉じた画面が描かれる次のTickで出す。
        if (!IsBusy(services)) deferredLoad_ = DeferredLoad{step.pathUtf8, step.discardRecovery};
        break;
    case LayoutFlowEffect::Quit:
        if (step.discardRecovery) services.action.DiscardCurrentRecovery();
        services.mainWindow.CloseNow();
        break;
    }
}

void LayoutDocumentController::OpenFilesScreen(bool saveAs, const LayoutDocumentServices& services) {
    rmlui::RmlUiWorkspaceView& view = *services.rmlUi.WorkspaceView();
    LayoutPersistenceAction& action = services.action;
    std::vector<LayoutFileEntry> folder = ListLayoutFolder(action.SaveDirectory());
    if (saveAs) {
        view.LayoutFiles().SetContent(rmlui::LayoutFilesMode::SaveAs, {}, std::move(folder),
                                      action.DocumentDisplayName());
    } else {
        // 名前を付けて保存は保存先のフォルダへ保存するので、別の場所も混ざる上の段はロードのときだけ出す。
        if (RemoveMissingRecentLayouts(services.userSettings.recentLayouts)) {
            SaveUserSettingsOrWarn(services.userSettings, "Failed to save user settings after pruning recent layouts.");
        }
        view.LayoutFiles().SetContent(rmlui::LayoutFilesMode::Load,
                                      ListExistingLayouts(services.userSettings.recentLayouts), std::move(folder), {},
                                      ListRs2Layouts(services.userSettings.pluginRoots));
    }
    view.OpenOverlay(OverlayKind::LayoutFiles);
}

void LayoutDocumentController::ShowUnsavedWarning(const LayoutDocumentServices& services) {
    rmlui::RmlUiConfirmDialog* const dialog = services.rmlUi.ConfirmDialog();
    if (dialog == nullptr) {
        unsavedAnswer_ = UnsavedChoice::Cancel;
        return;
    }
    rmlui::ConfirmDialogContent content;
    const std::string name = NameOrUntitled(services.action.DocumentDisplayName(), services.localization);
    content.message = std::vformat(services.localization.Text("layout.document.save_changes"),
                                   std::make_format_args(name));
    content.confirmLabel = services.localization.Text("layout.document.save");
    content.cancelLabel = services.localization.Text("layout.document.cancel");
    content.onConfirm = [this] { unsavedAnswer_ = UnsavedChoice::Save; };
    content.secondaryLabel = services.localization.Text("layout.document.dont_save");
    content.onSecondary = [this] { unsavedAnswer_ = UnsavedChoice::Discard; };
    content.onCancel = [this] { unsavedAnswer_ = UnsavedChoice::Cancel; };
    dialog->Open(std::move(content));
}

void LayoutDocumentController::ShowOverwriteConfirm(const std::string& pathUtf8,
                                                    const LayoutDocumentServices& services) {
    rmlui::RmlUiConfirmDialog* const dialog = services.rmlUi.ConfirmDialog();
    if (dialog == nullptr) {
        overwriteAnswer_ = false;
        return;
    }
    rmlui::ConfirmDialogContent content;
    const std::string name = LayoutNameFromPath(pathUtf8);
    content.message = std::vformat(services.localization.Text("layout.document.confirm_overwrite"),
                                   std::make_format_args(name));
    content.confirmLabel = services.localization.Text("layout.document.overwrite");
    content.cancelLabel = services.localization.Text("layout.document.cancel");
    content.danger = true;
    content.onConfirm = [this] { overwriteAnswer_ = true; };
    content.onCancel = [this] { overwriteAnswer_ = false; };
    dialog->Open(std::move(content));
}

void LayoutDocumentController::ShowQuitConfirm(const LayoutDocumentServices& services) {
    rmlui::RmlUiConfirmDialog* const dialog = services.rmlUi.ConfirmDialog();
    if (dialog == nullptr) {
        // 聞けないときは、閉じる操作をそのまま通す。
        quitAnswer_ = true;
        return;
    }
    rmlui::ConfirmDialogContent content;
    content.message = services.localization.Text("layout.document.confirm_quit");
    content.confirmLabel = services.localization.Text("layout.document.quit");
    content.cancelLabel = services.localization.Text("layout.document.cancel");
    content.onConfirm = [this] { quitAnswer_ = true; };
    content.onCancel = [this] { quitAnswer_ = false; };
    dialog->Open(std::move(content));
}

void LayoutDocumentController::BeforeRender(App& app, bool otherCaptureRequested) {
    if (pendingSavePathUtf8_.empty() || otherCaptureRequested) return;
    app.RequestBackBufferCapture(BackBufferCaptureStage::SceneOnly);
    saveCaptureRequested_ = true;
}

void LayoutDocumentController::AfterRender(App& app, LayoutPersistenceAction& action,
                                           const Localization& localization) {
    if (!std::exchange(saveCaptureRequested_, false)) return;
    // 撮れなくても保存はする(一覧ではファイル名だけの枠で出る)。
    std::vector<uint8_t> pixels;
    UINT width = 0, height = 0, rowPitch = 0;
    std::string thumbnail;
    if (app.TakeCapturedBackBuffer(pixels, width, height, rowPitch)) {
        thumbnail = EncodeLayoutThumbnail(pixels, width, height, rowPitch);
    }
    const std::string pathUtf8 = std::exchange(pendingSavePathUtf8_, {});
    const LayoutActionResult result = action.SaveRsl(pathUtf8, thumbnail.empty() ? nullptr : &thumbnail);
    if (result.Ok()) {
        recentToAddUtf8_ = result.savedPathUtf8;
    } else {
        failureMessage_ = LayoutActionErrorMessage(result, localization);
    }
    saveFinished_ = result.Ok();
}

} // namespace rs::app

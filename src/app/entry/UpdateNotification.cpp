#include "UpdateNotification.h"

#include <format>
#include <functional>
#include <optional>
#include <utility>
#include "AppRuntime.h"
#include "../action/UpdateAction.h"
#include "../action/UpdateScreenModel.h"

namespace rs::app {
namespace {
// トーストのボタンは、設定画面と同じ UpdateAction を通す。state は実行中ずっと生きている。
UpdateAction MakeUpdateAction(AppRuntimeState& state) {
    return UpdateAction(&state.updateChecker, &state.updateDownloader, &state.requestRestartForUpdate);
}

// 更新のトーストは、このキー1つに中身を入れ替えて出す。
constexpr const char* kUpdateToastKey = "update";

std::function<void()> ToastButtonAction(AppRuntimeState& state, UpdateButton button) {
    switch (button) {
    case UpdateButton::Update:
    case UpdateButton::OpenDownloadPage: return [&state] { MakeUpdateAction(state).Download(); };
    case UpdateButton::Cancel: return [&state] { MakeUpdateAction(state).Cancel(); };
    case UpdateButton::Restart: return [&state] { MakeUpdateAction(state).Restart(); };
    case UpdateButton::Retry:
    case UpdateButton::None: break;
    }
    return {};
}

void UpsertUpdateToast(AppRuntimeState& state, rmlui::RmlUiWorkspaceView& workspace, const UpdateToast& toast) {
    ToastPostOptions options;
    options.key = kUpdateToastKey;
    options.style = toast.style;
    options.sticky = toast.sticky;
    options.closable = toast.closable;
    options.keepOpenOnAction = toast.keepOpenOnAction;
    options.actionLabel = toast.buttonLabel;
    options.onAction = ToastButtonAction(state, toast.button);
    workspace.Toasts().UpsertKeyed(toast.severity, toast.text, std::move(options));
}

std::optional<UpdateToast> BuildCurrentUpdateToast(const AppRuntimeState& state) {
    return BuildUpdateToast(state.localization, MakeUpdateScreenInput(&state.updateChecker, &state.updateDownloader));
}

// 準備したあとに、確認がもっと新しい(または今と同じ)版を返したら、古い準備は捨てる。
// 捨てた準備の「準備完了」のトーストは残さない。
void DiscardStaleReadyUpdate(AppRuntimeState& state) {
    const UpdateCheckState checkState = state.updateChecker.State();
    if (checkState != UpdateCheckState::Available && checkState != UpdateCheckState::Current) return;
    if (!state.updateDownloader.DiscardReadyIfVersionDiffers(state.updateChecker.Release().version)) return;
    if (rmlui::RmlUiWorkspaceView* workspace = state.rmlUiIntegration.WorkspaceView()) {
        workspace->Toasts().DismissKey(kUpdateToastKey);
    }
}

// ダウンロードの状態に合わせて、更新のトーストの中身を入れ替える。進行中は毎フレーム(同じ中身なら何も起きない)、
// 終わりの状態は試行ごとに1回だけ(利用者が×で閉じたあとに出し直さない)。
void PostDownloadStateToast(AppRuntimeState& state, rmlui::RmlUiWorkspaceView& workspace) {
    UpdateSessionState& session = state.update;
    std::uint64_t* noticeAttempt = nullptr;
    switch (state.updateDownloader.State()) {
    case UpdateDownloadState::Idle: return;
    case UpdateDownloadState::Downloading:
    case UpdateDownloadState::Verifying:
    case UpdateDownloadState::Extracting: break;
    case UpdateDownloadState::Ready: noticeAttempt = &session.readyNoticeAttempt; break;
    case UpdateDownloadState::Failed: noticeAttempt = &session.failedNoticeAttempt; break;
    case UpdateDownloadState::Cancelled: noticeAttempt = &session.cancelledNoticeAttempt; break;
    }
    const std::uint64_t attempt = state.updateDownloader.Attempt();
    if (noticeAttempt != nullptr && *noticeAttempt == attempt) return;
    if (noticeAttempt != nullptr) *noticeAttempt = attempt;
    if (const std::optional<UpdateToast> toast = BuildCurrentUpdateToast(state)) {
        UpsertUpdateToast(state, workspace, *toast);
    } else {
        workspace.Toasts().DismissKey(kUpdateToastKey);
    }
}

// 失敗・ロールバックの通知。重大度は警告のままで、閉じるまで残す。
void PostStickyWarning(rmlui::RmlUiWorkspaceView& workspace, std::string text) {
    ToastPostOptions options;
    options.sticky = true;
    workspace.Toasts().PostWithOptions(ToastSeverity::Warning, std::move(text), std::move(options));
}
} // namespace

void StartUpdateCheck(AppRuntimeState& state) {
    if (IsUnattendedRun(state.cli)) return;
    CleanupStaleUpdateDirectories();
    state.updateChecker.Request(state.userSettings);
}

void PollUpdateCheck(AppRuntimeState& state) {
    if (!state.updateChecker.Poll()) return;
    state.updateDownloader.InvalidateInstallDirectoryCheck();
    if (state.updateChecker.State() != UpdateCheckState::Available) return;
    // 新しい通知を出す前に、古い準備を捨てておく(捨てたあとの状態で通知の中身が決まる)。
    DiscardStaleReadyUpdate(state);
    const ReleaseInfo& release = state.updateChecker.Release();
    if (state.userSettings.dismissedUpdateVersion == release.version) return;
    rmlui::RmlUiWorkspaceView* workspace = state.rmlUiIntegration.WorkspaceView();
    if (workspace == nullptr) return;
    if (const std::optional<UpdateToast> toast = BuildCurrentUpdateToast(state)) {
        UpsertUpdateToast(state, *workspace, *toast);
    }
    state.userSettings.dismissedUpdateVersion = release.version;
    SaveUserSettingsOrWarn(state.userSettings, "Failed to save notified update version.");
}

void PollUpdateDownload(AppRuntimeState& state) {
    if (IsUnattendedRun(state.cli)) return;
    state.updateDownloader.Poll();
    DiscardStaleReadyUpdate(state);

    // ワークスペースが無いフレームは出さずに、次のフレームでもう一度試す。
    rmlui::RmlUiWorkspaceView* workspace = state.rmlUiIntegration.WorkspaceView();
    if (workspace == nullptr) return;
    const Localization& localization = state.localization;
    UpdateSessionState& session = state.update;
    PostDownloadStateToast(state, *workspace);
    if (session.succeeded) {
        const std::string version = session.succeeded->toVersion;
        ToastPostOptions options;
        options.style = ToastStyle::Update;
        workspace->Toasts().PostWithOptions(ToastSeverity::Info,
                                            std::vformat(localization.Text("game_settings.about.updated_toast"),
                                                         std::make_format_args(version)),
                                            std::move(options));
        session.succeeded.reset();
    }
    if (session.rolledBackNotice) {
        PostStickyWarning(*workspace, localization.Text("game_settings.about.rolled_back_toast"));
        session.rolledBackNotice = false;
    }
}

void PrepareUpdateRestart(AppRuntimeState& state) {
    if (state.updateDownloader.State() != UpdateDownloadState::Ready) return;
    state.restartForUpdate = true;
    state.update.stagingDirectory = state.updateDownloader.StagingDirectory();
    state.update.toVersion = state.updateDownloader.ToVersion();
}
} // namespace rs::app

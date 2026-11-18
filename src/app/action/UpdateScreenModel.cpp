#include "UpdateScreenModel.h"

#include <algorithm>
#include <array>
#include <format>
#include <utility>

namespace rs::app {
namespace {

constexpr double kBytesPerMegabyte = 1024.0 * 1024.0;

std::string_view FailureTextKey(UpdateDownloadFailure failure) {
    switch (failure) {
    case UpdateDownloadFailure::SizeMismatch:
    case UpdateDownloadFailure::HashMismatch: return "game_settings.about.failure_verify";
    case UpdateDownloadFailure::ExtractFailed: return "game_settings.about.failure_extract";
    case UpdateDownloadFailure::NotEnoughSpace: return "game_settings.about.failure_space";
    case UpdateDownloadFailure::NotWritable: return "game_settings.about.failure_not_writable";
    case UpdateDownloadFailure::DownloadFailed:
    case UpdateDownloadFailure::None: break;
    }
    return "game_settings.about.failure_download";
}

// 進み具合(%、MB)の書式は、画面とトーストで共通。文言だけキーで変える。
std::string DownloadingText(const Localization& localization, std::string_view textKey,
                            const UpdateDownloadProgress& progress) {
    const std::uint64_t total = std::max<std::uint64_t>(progress.totalBytes, 1);
    const std::uint64_t received = std::min(progress.receivedBytes, total);
    const int percent = static_cast<int>(received * 100 / total);
    const std::string receivedMegabytes = std::format("{:.1f}", static_cast<double>(received) / kBytesPerMegabyte);
    const std::string totalMegabytes = std::format("{:.1f}", static_cast<double>(total) / kBytesPerMegabyte);
    return std::vformat(localization.Text(textKey), std::make_format_args(percent, receivedMegabytes, totalMegabytes));
}

// ダウンロードの途中・済み・失敗のとき。そうでなければfalse。
bool BuildDownloadScreen(const Localization& localization, const UpdateScreenInput& input, UpdateScreen& screen) {
    switch (input.download) {
    case UpdateDownloadState::Downloading:
        screen.status = DownloadingText(localization, "game_settings.about.downloading", input.progress);
        screen.button = UpdateButton::Cancel;
        return true;
    case UpdateDownloadState::Verifying:
        screen.status = localization.Text("game_settings.about.checking");
        return true;
    case UpdateDownloadState::Extracting:
        screen.status = localization.Text("game_settings.about.extracting");
        return true;
    case UpdateDownloadState::Ready:
        screen.status = std::vformat(localization.Text("game_settings.about.prepared"),
                                     std::make_format_args(input.preparedVersion));
        screen.button = UpdateButton::Restart;
        return true;
    case UpdateDownloadState::Failed: {
        const std::string reason = localization.Text(FailureTextKey(input.failure));
        screen.status = std::vformat(localization.Text("game_settings.about.prepare_failed"),
                                     std::make_format_args(reason));
        screen.button = UpdateButton::Retry;
        return true;
    }
    case UpdateDownloadState::Idle:
    case UpdateDownloadState::Cancelled: break;
    }
    return false;
}

} // namespace

UpdateScreen BuildUpdateScreen(const Localization& localization, const UpdateScreenInput& input) {
    UpdateScreen screen;
    if (BuildDownloadScreen(localization, input, screen)) return screen;
    // Idle と Cancelled は、確認の結果だけで決まる(Cancelled は Idle と同じ見え方)。
    screen.status = localization.Text(UpdateCheckStateTextKey(input.check));
    if (input.check != UpdateCheckState::Available) return screen;
    screen.status = std::vformat(screen.status, std::make_format_args(input.latestVersion));
    if (input.availability == InAppUpdateAvailability::Available) {
        screen.button = UpdateButton::Update;
        return screen;
    }
    screen.button = UpdateButton::OpenDownloadPage;
    if (input.availability == InAppUpdateAvailability::NotWritable) {
        screen.note = localization.Text("game_settings.about.not_writable_note");
    }
    return screen;
}

UpdateScreenInput MakeUpdateScreenInput(const UpdateChecker* checker, const UpdateDownloader* downloader) {
    UpdateScreenInput input;
    if (checker != nullptr) {
        input.check = checker->State();
        input.latestVersion = checker->Release().version;
    }
    if (downloader != nullptr) {
        if (checker != nullptr) input.availability = downloader->Availability(checker->Release());
        input.download = downloader->State();
        input.failure = downloader->Failure();
        input.progress = downloader->Progress();
        input.preparedVersion = downloader->ToVersion();
    }
    return input;
}

namespace {

std::string_view ButtonLabelKey(UpdateButton button) {
    switch (button) {
    case UpdateButton::Update: return "game_settings.about.update_button";
    case UpdateButton::Cancel: return "game_settings.about.cancel_button";
    case UpdateButton::Restart: return "game_settings.about.restart_button";
    case UpdateButton::Retry: return "game_settings.about.retry_button";
    case UpdateButton::OpenDownloadPage: return "game_settings.about.open_download_page";
    case UpdateButton::None: break;
    }
    return {};
}

UpdateToast MakeToast(const Localization& localization, std::string text, UpdateButton button, bool closable) {
    UpdateToast toast;
    toast.text = std::move(text);
    toast.button = button;
    if (button != UpdateButton::None) toast.buttonLabel = localization.Text(ButtonLabelKey(button));
    toast.closable = closable;
    toast.keepOpenOnAction = button != UpdateButton::None && button != UpdateButton::OpenDownloadPage;
    return toast;
}

} // namespace

std::optional<UpdateToast> BuildUpdateToast(const Localization& localization, const UpdateScreenInput& input) {
    switch (input.download) {
    case UpdateDownloadState::Downloading:
        return MakeToast(localization, DownloadingText(localization, "game_settings.about.update_toast_downloading",
                                                       input.progress),
                         UpdateButton::Cancel, false);
    case UpdateDownloadState::Verifying:
        return MakeToast(localization, localization.Text("game_settings.about.update_toast_checking"),
                         UpdateButton::None, false);
    case UpdateDownloadState::Extracting:
        return MakeToast(localization, localization.Text("game_settings.about.update_toast_extracting"),
                         UpdateButton::None, false);
    case UpdateDownloadState::Ready:
        return MakeToast(localization, localization.Text("game_settings.about.download_ready_toast"),
                         UpdateButton::Restart, true);
    case UpdateDownloadState::Failed: {
        UpdateToast toast = MakeToast(localization, localization.Text("game_settings.about.update_failed_toast"),
                                      UpdateButton::None, true);
        toast.severity = ToastSeverity::Warning;
        toast.style = ToastStyle::Default;
        return toast;
    }
    case UpdateDownloadState::Idle:
    case UpdateDownloadState::Cancelled: break;
    }
    if (input.check != UpdateCheckState::Available) return std::nullopt;
    const UpdateButton button = input.availability == InAppUpdateAvailability::Available
                                    ? UpdateButton::Update
                                    : UpdateButton::OpenDownloadPage;
    return MakeToast(localization,
                     std::vformat(localization.Text("game_settings.about.update_toast"),
                                  std::make_format_args(input.latestVersion)),
                     button, true);
}

std::string_view UpdateButtonName(UpdateButton button) {
    constexpr std::array<const char*, 6> kNames{"", "update", "cancel", "restart", "retry", "page"};
    return kNames[static_cast<std::size_t>(button)];
}

} // namespace rs::app

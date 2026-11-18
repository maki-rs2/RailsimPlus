#include "UpdateStartup.h"

#include <spdlog/spdlog.h>


namespace rs::app {
namespace {
namespace fs = std::filesystem;

bool LaunchOrLog(const fs::path& executable, const fs::path& workingDirectory, const UpdateApplyOptions& options) {
    const bool launched = LaunchExecutable(executable, workingDirectory, options);
    if (!launched) spdlog::error("[update] Could not launch the restored executable");
    return launched;
}

bool RemoveMarker(const fs::path& markerPath) {
    std::error_code error;
    fs::remove(markerPath, error);
    if (error) spdlog::warn("[update] Could not remove update marker: {}", error.message());
    return !error;
}

StartupMarkerOutcome RollbackAndRelaunch(const fs::path& executablePath, const UpdateMarker& marker,
                                         const UpdateApplyOptions& options) {
    const fs::path installDirectory = executablePath.parent_path();
    StartupMarkerOutcome outcome;
    outcome.exitProcess = true;
    spdlog::warn("[update] Previous update did not finish starting; rolling back to {}", marker.fromVersion);
    // 戻しきれていないのに起動し直すと、新しい版がまた戻す処理を始めて、起動を繰り返してしまう。
    if (!RollbackUpdate(installDirectory, marker, options)) {
        spdlog::error("[update] Rollback was incomplete; not relaunching");
        return outcome;
    }
    LaunchOrLog(executablePath, installDirectory, options);
    return outcome;
}
} // namespace

StartupMarkerOutcome RunStartupMarkerCheck(const fs::path& executablePath, std::string_view currentVersion,
                                           const UpdateApplyOptions& options) {
    const fs::path installDirectory = executablePath.parent_path();
    const fs::path markerPath = UpdateMarkerPath(installDirectory);
    UpdateMarkerRead read = ReadUpdateMarker(markerPath);
    StartupMarkerOutcome outcome;
    switch (DecideStartupAction(read, currentVersion)) {
    case StartupAction::None:
        break;
    case StartupAction::CorruptMarker: {
        // ほかのファイルには触らず、マーカーだけ脇へ寄せて、そのまま起動する。
        std::error_code error;
        fs::path corruptPath = markerPath;
        corruptPath += L".corrupt";
        fs::rename(markerPath, corruptPath, error);
        spdlog::warn("[update] Update marker is corrupt; moved aside ({})", error ? error.message() : "ok");
        break;
    }
    case StartupAction::RollbackAndRelaunch:
        return RollbackAndRelaunch(executablePath, read.marker, options);
    case StartupAction::NotifyRolledBack:
        // 戻したあとの掃除はここで1回だけやってみる。消せなくても害はない。
        CleanupUpdateFiles(installDirectory, read.marker.plan);
        RemoveMarker(markerPath);
        outcome.rolledBackNotice = true;
        break;
    case StartupAction::DiscardStaleMarker:
        // 手で差し替えられた。トーストは出さない。
        CleanupUpdateFiles(installDirectory, read.marker.plan);
        RemoveMarker(markerPath);
        break;
    case StartupAction::ConfirmPendingStart: {
        // 最初のフレームまで届いたら ConfirmUpdateSucceeded が消す。届かなければ、次の起動で戻す。
        read.marker.attempts = 1;
        std::string error;
        if (!WriteUpdateMarker(markerPath, read.marker, error)) {
            spdlog::warn("[update] Could not record the start attempt: {}", error);
        }
        break;
    }
    }
    return outcome;
}

std::optional<UpdateSuccess> ConfirmUpdateSucceeded(const fs::path& installDirectory) {
    const fs::path markerPath = UpdateMarkerPath(installDirectory);
    const UpdateMarkerRead read = ReadUpdateMarker(markerPath);
    if (read.status != UpdateMarkerReadStatus::Ok || read.marker.state != UpdateMarkerState::Pending) return std::nullopt;
    // 先にマーカーを消す。ここで落ちても `.bak` が残るだけで、次の起動が成功を戻してしまうことは無い。
    if (!RemoveMarker(markerPath)) return std::nullopt;
    if (!CleanupUpdateFiles(installDirectory, read.marker.plan)) {
        spdlog::warn("[update] Some backup files could not be removed");
    }
    return UpdateSuccess{read.marker.fromVersion, read.marker.toVersion};
}

void ConfirmUpdateOnFirstFrame(UpdateSessionState& session, bool unattendedRun) {
    if (session.firstFrameHandled) return;
    session.firstFrameHandled = true;
    if (unattendedRun || session.executablePath.empty()) return;
    session.succeeded = ConfirmUpdateSucceeded(session.executablePath.parent_path());
}

} // namespace rs::app

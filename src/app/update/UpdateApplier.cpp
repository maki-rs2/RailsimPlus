#include "UpdateApplier.h"

#include <spdlog/spdlog.h>
#include <thread>
#include <windows.h>

#include "../../platform/file/DirectoryWritable.h"
#include "../../platform/file/DiskSpace.h"
#include "../../platform/process/ProcessLaunch.h"
#include "../../platform/text/Encoding.h"

namespace rs::app {
namespace {
namespace fs = std::filesystem;

fs::path Resolve(const fs::path& directory, const std::string& relativePath) {
    return directory / fs::path(rs::platform_text::Utf8ToWide(relativePath));
}

fs::path WithSuffix(const fs::path& path, const wchar_t* suffix) {
    fs::path result = path;
    result += suffix;
    return result;
}

bool PathExists(const fs::path& path) {
    std::error_code error;
    return fs::exists(fs::symlink_status(path, error)) && !error;
}

// 差し替えのリネームは全部ここを通す。同じボリュームならリネーム、違えば(allowCopy のとき)コピーになる。
// ERROR_SHARING_VIOLATION のときだけやり直す。
bool RenameForUpdate(const fs::path& from, const fs::path& to, bool allowCopy, const UpdateApplyOptions& options) {
    const DWORD flags = MOVEFILE_REPLACE_EXISTING | (allowCopy ? MOVEFILE_COPY_ALLOWED : 0);
    for (int attempt = 0;; ++attempt) {
        if (::MoveFileExW(from.c_str(), to.c_str(), flags)) return true;
        const DWORD error = ::GetLastError();
        if (error != ERROR_SHARING_VIOLATION || attempt >= options.renameRetryCount) {
            spdlog::warn("[update] Rename failed ({}): {} -> {}", error, rs::platform_text::WideToUtf8(from.wstring()),
                         rs::platform_text::WideToUtf8(to.wstring()));
            return false;
        }
        if (options.onRenameRetry) options.onRenameRetry();
        if (options.renameRetryInterval.count() > 0) std::this_thread::sleep_for(options.renameRetryInterval);
    }
}

// 1ファイルを元に戻す。本名を `.failed` へ退避するのは、戻す `.bak` がある(新しい本名が入っている)ときと、
// 追加したファイルのときだけ。`.bak` が無い置換・削除では、本名はまだ元のファイルなので触らない。
bool RestoreFile(const fs::path& installDirectory, const UpdateOperation& operation, const UpdateApplyOptions& options) {
    const fs::path mainPath = Resolve(installDirectory, operation.path);
    const fs::path backupPath = WithSuffix(mainPath, L".bak");
    const fs::path newPath = WithSuffix(mainPath, L".new");
    const bool hasBackup = PathExists(backupPath);
    bool ok = true;
    if (hasBackup || operation.kind == UpdateOperationKind::Add) {
        if (PathExists(mainPath)) ok = RenameForUpdate(mainPath, WithSuffix(mainPath, L".failed"), false, options);
    }
    if (hasBackup && ok) ok = RenameForUpdate(backupPath, mainPath, false, options);
    if (PathExists(newPath)) {
        std::error_code error;
        fs::remove(newPath, error);
        if (error) ok = false;
    }
    return ok;
}

bool WriteMarkerOrLog(const fs::path& installDirectory, const UpdateMarker& marker) {
    std::string error;
    if (WriteUpdateMarker(UpdateMarkerPath(installDirectory), marker, error)) return true;
    spdlog::warn("[update] Could not write update marker: {}", error);
    return false;
}

// staging の全ファイルを、インストール先の `<名前>.new` へ置く。既存のファイルには触らない。
bool PlaceNewFiles(const UpdateApplyRequest& request, const UpdateApplyOptions& options) {
    for (const std::string& directory : request.plan.directoriesToCreate) {
        std::error_code error;
        fs::create_directories(Resolve(request.installDirectory, directory), error);
        if (error) return false;
    }
    for (const UpdateOperation& operation : request.plan.operations) {
        if (operation.kind == UpdateOperationKind::Delete) continue;
        const fs::path target = Resolve(request.installDirectory, operation.path);
        if (!RenameForUpdate(Resolve(request.stagingDirectory, operation.path), WithSuffix(target, L".new"), true, options)) {
            return false;
        }
    }
    return true;
}

// ファイルごとに「本名 → .bak」「.new → 本名」。削除は `.bak` へ退避するだけ。
bool SwapFiles(const UpdateApplyRequest& request, const UpdateApplyOptions& options) {
    for (const UpdateOperation& operation : request.plan.operations) {
        const fs::path target = Resolve(request.installDirectory, operation.path);
        if (operation.kind != UpdateOperationKind::Add &&
            !RenameForUpdate(target, WithSuffix(target, L".bak"), false, options)) return false;
        if (operation.kind != UpdateOperationKind::Delete &&
            !RenameForUpdate(WithSuffix(target, L".new"), target, false, options)) return false;
    }
    return true;
}
} // namespace

bool RollbackUpdate(const fs::path& installDirectory, UpdateMarker marker, const UpdateApplyOptions& options) {
    bool complete = true;
    // 1ファイルが失敗しても、残りは続けて戻す。
    for (const UpdateOperation& operation : marker.plan.operations) {
        if (!RestoreFile(installDirectory, operation, options)) complete = false;
    }
    if (!complete) return false;
    marker.state = UpdateMarkerState::RolledBack;
    if (WriteMarkerOrLog(installDirectory, marker)) return true;
    // 書けないままだと、次の起動で戻す処理をまた始めてしまう。マーカーを消せれば、戻し終えたことにできる。
    std::error_code error;
    fs::remove(UpdateMarkerPath(installDirectory), error);
    return !error;
}

bool LaunchExecutable(const fs::path& executable, const fs::path& workingDirectory, const UpdateApplyOptions& options) {
    return options.launchProcess ? options.launchProcess(executable, workingDirectory)
                                 : rs::platform_process::LaunchProcess(executable, workingDirectory);
}

bool CleanupUpdateFiles(const fs::path& installDirectory, const UpdatePlan& plan) {
    bool allRemoved = true;
    for (const UpdateOperation& operation : plan.operations) {
        const fs::path mainPath = Resolve(installDirectory, operation.path);
        for (const wchar_t* suffix : {L".bak", L".new", L".failed"}) {
            std::error_code error;
            fs::remove(WithSuffix(mainPath, suffix), error);
            if (error) {
                spdlog::warn("[update] Could not remove leftover file: {}", operation.path);
                allRemoved = false;
            }
        }
    }
    return allRemoved;
}

UpdateApplyStatus ApplyUpdate(const UpdateApplyRequest& request, const UpdateApplyOptions& options,
                              std::string& outError) {
    // 事前確認。ここまでは何も書かない。
    if (!rs::platform_file::IsDirectoryWritable(request.installDirectory)) {
        outError = "install directory is not writable";
        return UpdateApplyStatus::Failed;
    }
    std::uint64_t requiredBytes = 0;
    for (const UpdateOperation& operation : request.plan.operations) {
        if (operation.kind == UpdateOperationKind::Delete) continue;
        std::error_code error;
        const std::uintmax_t size = fs::file_size(Resolve(request.stagingDirectory, operation.path), error);
        if (error) {
            outError = "staged file is missing: " + operation.path;
            return UpdateApplyStatus::Failed;
        }
        requiredBytes += size;
    }
    const std::optional<std::uint64_t> freeBytes = options.queryFreeBytes
        ? options.queryFreeBytes(request.installDirectory)
        : rs::platform_file::QueryFreeDiskBytes(request.installDirectory);
    if (freeBytes && !HasEnoughDiskSpace(requiredBytes, *freeBytes)) {
        outError = "not enough free disk space";
        return UpdateApplyStatus::InsufficientSpace;
    }

    UpdateMarker marker;
    marker.fromVersion = request.fromVersion;
    marker.toVersion = request.toVersion;
    marker.plan = request.plan;
    marker.state = UpdateMarkerState::Applying;
    if (!WriteMarkerOrLog(request.installDirectory, marker)) {
        outError = "could not write update marker";
        return UpdateApplyStatus::Failed;
    }

    if (PlaceNewFiles(request, options) && SwapFiles(request, options)) {
        marker.state = UpdateMarkerState::Pending;
        marker.attempts = 0;
        if (WriteMarkerOrLog(request.installDirectory, marker)) return UpdateApplyStatus::Applied;
        outError = "could not update marker to pending";
    } else {
        outError = "could not replace files";
    }
    return RollbackUpdate(request.installDirectory, marker, options) ? UpdateApplyStatus::Failed
                                                                     : UpdateApplyStatus::RollbackIncomplete;
}

} // namespace rs::app

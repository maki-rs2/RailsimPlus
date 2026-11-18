#include "UpdateRestart.h"

#include <spdlog/spdlog.h>

#include "../../platform/file/FileRead.h"
#include "../../platform/text/Encoding.h"

namespace rs::app {
namespace {
namespace fs = std::filesystem;

// staging の通常ファイルを、相対パス(UTF-8、`/` 区切り)で列挙する。
std::vector<std::string> ListStagedFiles(const fs::path& stagingDirectory) {
    std::vector<std::string> files;
    std::error_code error;
    for (fs::recursive_directory_iterator it(stagingDirectory, error), end; !error && it != end; it.increment(error)) {
        if (!it->is_regular_file(error)) continue;
        files.push_back(rs::platform_text::WideToUtf8(it->path().lexically_relative(stagingDirectory).generic_wstring()));
    }
    return files;
}

} // namespace

bool ApplyUpdateAndRelaunch(const UpdateRestartRequest& request, const UpdateApplyOptions& options) {
    const fs::path installDirectory = request.executablePath.parent_path();
    UpdateApplyRequest apply;
    apply.installDirectory = installDirectory;
    apply.stagingDirectory = request.stagingDirectory;
    apply.fromVersion = request.fromVersion;
    apply.toVersion = request.toVersion;
    apply.plan = BuildUpdatePlan(
        ListStagedFiles(request.stagingDirectory),
        rs::platform_file::ReadFileText(installDirectory / fs::path(rs::platform_text::Utf8ToWide(std::string(kInstalledFilesName))))
            .value_or(std::string{}),
        [&](const std::string& relativePath) {
            std::error_code error;
            return fs::exists(installDirectory / fs::path(rs::platform_text::Utf8ToWide(relativePath)), error);
        });

    std::string error;
    const UpdateApplyStatus status = ApplyUpdate(apply, options, error);
    if (status != UpdateApplyStatus::Applied) {
        spdlog::error("[update] Update was not applied: {}", error);
        return false;
    }
    if (LaunchExecutable(request.executablePath, installDirectory, options)) return true;

    // 新しい実行ファイルを起動できなかった(ウイルス対策ソフトに隔離された場合など)。その場で戻す。
    spdlog::error("[update] Could not launch the updated executable; rolling back");
    const UpdateMarkerRead read = ReadUpdateMarker(UpdateMarkerPath(installDirectory));
    if (read.status == UpdateMarkerReadStatus::Ok && RollbackUpdate(installDirectory, read.marker, options)) {
        if (!LaunchExecutable(request.executablePath, installDirectory, options)) {
            spdlog::error("[update] Could not launch the restored executable");
        }
    } else {
        spdlog::error("[update] Rollback after launch failure was incomplete");
    }
    return false;
}

} // namespace rs::app

#include "UpdateDownloader.h"

#include <chrono>
#include <system_error>
#include <utility>
#include <spdlog/spdlog.h>

#include "UpdatePlan.h"
#include "../../platform/file/AtomicFileWrite.h"
#include "../../platform/file/DirectoryWritable.h"
#include "../../platform/file/DiskSpace.h"
#include "../../platform/file/ExecutableDirectory.h"
#include "../../platform/file/Sha256.h"
#include "../../platform/file/UserDataDirectory.h"
#include "../../platform/file/ZipArchive.h"
#include "../../platform/text/Encoding.h"

namespace rs::app {
namespace {
namespace fs = std::filesystem;

constexpr const char* kPackageFileName = "package.zip";
constexpr const char* kStagingDirectoryName = "staging";
// ZIPを展開したときの上限。配布物の大きさに対して十分大きく、壊れたZIPが際限なく書くことは防げる値。
constexpr std::uint64_t kMaxExtractedFileBytes = std::uint64_t{512} * 1024 * 1024;
constexpr rs::platform_file::ZipLimits kExtractLimits{std::uint64_t{1024} * 1024 * 1024, 20000};
// 展開に必要な空きは、ZIPのサイズ(ダウンロード分)と展開後のぶんで、ZIPの2倍程度を見込む。
constexpr std::uint64_t kFreeSpaceFactor = 2;

// 作業の結果。ワーカーはこの値を返すだけにして、保留結果への書き込みは1か所にする。
struct Outcome {
    UpdateDownloadState state = UpdateDownloadState::Failed;
    UpdateDownloadFailure failure = UpdateDownloadFailure::None;
};

Outcome Failed(UpdateDownloadFailure failure) { return {UpdateDownloadState::Failed, failure}; }
Outcome Cancelled() { return {UpdateDownloadState::Cancelled, UpdateDownloadFailure::None}; }
Outcome Succeeded() { return {UpdateDownloadState::Ready, UpdateDownloadFailure::None}; }

// ZIPに入っていたファイルの相対パス(接頭辞なし、`/` 区切り、1行1パス、LF)。次回の更新が「前回の一覧」として読む。
std::string BuildInstalledFilesText(const std::vector<rs::platform_file::ZipEntryInfo>& entries) {
    std::string text;
    for (const rs::platform_file::ZipEntryInfo& entry : entries) {
        if (entry.isDirectory) continue;
        const std::string relative = rs::platform_file::ArchiveRelativePath(entry.name);
        if (relative.empty()) continue;
        text += relative;
        text += '\n';
    }
    return text;
}

bool ExtractPackage(const fs::path& package, const fs::path& staging) {
    std::string error;
    const std::vector<rs::platform_file::ZipEntryInfo> entries = rs::platform_file::ListZipEntries(package, error);
    if (entries.empty()) {
        spdlog::warn("[update] Could not read the package: {}", error);
        return false;
    }
    for (const rs::platform_file::ZipEntryInfo& entry : entries) {
        if (entry.uncompressedSize > kMaxExtractedFileBytes) {
            spdlog::warn("[update] A package entry is too large: {}", entry.name);
            return false;
        }
    }
    if (!rs::platform_file::ExtractZip(package, staging, kExtractLimits, error)) {
        spdlog::warn("[update] Could not extract the package: {}", error);
        return false;
    }
    const fs::path listPath = staging / fs::path(rs::platform_text::Utf8ToWide(std::string(kInstalledFilesName)));
    if (!rs::platform_file::WriteFileAtomically(listPath, BuildInstalledFilesText(entries), error)) {
        spdlog::warn("[update] Could not write the file list: {}", error);
        return false;
    }
    return true;
}

} // namespace

InAppUpdateAvailability EvaluateInAppUpdate(const ReleaseInfo& release, bool installDirectoryWritable) {
    if (!release.HasInstallableAsset()) return InAppUpdateAvailability::NoInstallableAsset;
    return installDirectoryWritable ? InAppUpdateAvailability::Available : InAppUpdateAvailability::NotWritable;
}

fs::path DefaultUpdateWorkRoot() { return rs::platform_file::UserDataDirectory() / L"update"; }

void CleanupStaleUpdateDirectories(const fs::path& workRoot) {
    std::error_code error;
    if (!fs::is_directory(workRoot, error)) return;
    for (fs::directory_iterator it(workRoot, error), end; !error && it != end; it.increment(error)) {
        std::error_code removeError;
        fs::remove_all(it->path(), removeError);
        if (removeError) spdlog::warn("[update] Could not remove a stale update directory: {}", removeError.message());
    }
}

UpdateDownloader::UpdateDownloader(UpdateDownloaderOptions options) : options_(std::move(options)) {
    if (options_.workRoot.empty()) options_.workRoot = DefaultUpdateWorkRoot();
    if (!options_.download) options_.download = rs::platform_net::HttpDownloadToFile;
    if (!options_.queryFreeBytes) options_.queryFreeBytes = rs::platform_file::QueryFreeDiskBytes;
    if (!options_.installDirectoryWritable) {
        options_.installDirectoryWritable = [] {
            return rs::platform_file::IsDirectoryWritable(rs::platform_file::ExecutableDirectory());
        };
    }
}

UpdateDownloader::~UpdateDownloader() = default;

InAppUpdateAvailability UpdateDownloader::Availability(const ReleaseInfo& release) const {
    // 資産が無いときは、書き込めるかを調べない(フォルダへ触らない)。
    if (!release.HasInstallableAsset()) return InAppUpdateAvailability::NoInstallableAsset;
    if (!installWritableCache_) installWritableCache_ = options_.installDirectoryWritable();
    return EvaluateInAppUpdate(release, *installWritableCache_);
}

UpdateDownloadProgress UpdateDownloader::Progress() const {
    return {receivedBytes_.load(), totalBytes_.load()};
}

bool UpdateDownloader::Start(const ReleaseInfo& release) {
    const UpdateDownloadState current = state_.load();
    if (current != UpdateDownloadState::Idle && current != UpdateDownloadState::Failed &&
        current != UpdateDownloadState::Cancelled) {
        return false;
    }
    ++attempt_;
    failure_ = UpdateDownloadFailure::None;
    stagingDirectory_.clear();
    toVersion_ = release.version;
    installWritableCache_.reset(); // 開始のときは確認し直す
    const InAppUpdateAvailability availability = Availability(release);
    if (availability != InAppUpdateAvailability::Available) {
        failure_ = availability == InAppUpdateAvailability::NotWritable ? UpdateDownloadFailure::NotWritable
                                                                        : UpdateDownloadFailure::DownloadFailed;
        state_ = UpdateDownloadState::Failed;
        return false;
    }
    receivedBytes_ = 0;
    totalBytes_ = release.assetSize;
    const std::chrono::steady_clock::rep ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    workDirectory_ = options_.workRoot / fs::path("u" + std::to_string(ticks) + "_" + std::to_string(attempt_));
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        pending_.reset();
    }
    state_ = UpdateDownloadState::Downloading;
    try {
        worker_ = std::jthread([this, release, directory = workDirectory_](const std::stop_token& stop) {
            Run(release, directory, stop);
        });
    } catch (const std::exception& error) {
        spdlog::warn("[update] Could not start the download: {}", error.what());
        failure_ = UpdateDownloadFailure::DownloadFailed;
        state_ = UpdateDownloadState::Failed;
        return false;
    }
    return true;
}

void UpdateDownloader::Run(const ReleaseInfo& release, const fs::path& workDirectory, const std::stop_token& stop) {
    Outcome outcome = Failed(UpdateDownloadFailure::DownloadFailed);
    fs::path staging = workDirectory / kStagingDirectoryName;
    try {
        std::error_code error;
        fs::create_directories(workDirectory, error);
        const fs::path package = workDirectory / kPackageFileName;
        const std::optional<std::uint64_t> freeBytes = options_.queryFreeBytes(workDirectory);
        if (error) {
            spdlog::warn("[update] Could not create the work directory: {}", error.message());
        } else if (freeBytes && *freeBytes < release.assetSize * kFreeSpaceFactor) {
            outcome = Failed(UpdateDownloadFailure::NotEnoughSpace);
        } else if (!IsAllowedDownloadUrl(release.assetUrl)) {
            spdlog::warn("[update] The asset URL is not allowed");
        } else {
            rs::platform_net::HttpDownloadOptions downloadOptions;
            downloadOptions.userAgent = std::string("RailsimPlus/") + RS_APP_VERSION;
            downloadOptions.maxBytes = release.assetSize;
            downloadOptions.receivedBytes = &receivedBytes_;
            downloadOptions.stopToken = stop;
            const rs::platform_net::HttpDownloadResult result =
                options_.download(release.assetUrl, package, downloadOptions);
            if (result == rs::platform_net::HttpDownloadResult::Cancelled || stop.stop_requested()) {
                outcome = Cancelled();
            } else if (result == rs::platform_net::HttpDownloadResult::TooLarge) {
                outcome = Failed(UpdateDownloadFailure::SizeMismatch);
            } else if (result != rs::platform_net::HttpDownloadResult::Ok) {
                spdlog::warn("[update] Download failed ({})", static_cast<int>(result));
            } else if (fs::file_size(package, error) != release.assetSize || error) {
                outcome = Failed(UpdateDownloadFailure::SizeMismatch);
            } else {
                state_ = UpdateDownloadState::Verifying;
                const std::optional<std::string> hash = rs::platform_file::Sha256HexOfFile(package);
                if (stop.stop_requested()) {
                    outcome = Cancelled();
                } else if (!hash || *hash != release.assetSha256) {
                    outcome = Failed(UpdateDownloadFailure::HashMismatch);
                } else {
                    state_ = UpdateDownloadState::Extracting;
                    outcome = ExtractPackage(package, staging) ? Succeeded() : Failed(UpdateDownloadFailure::ExtractFailed);
                    if (stop.stop_requested()) outcome = Cancelled();
                }
            }
        }
    } catch (const std::exception& error) {
        spdlog::warn("[update] Preparing the update failed: {}", error.what());
        outcome = Failed(UpdateDownloadFailure::DownloadFailed);
    }
    if (outcome.state == UpdateDownloadState::Ready) {
        // ZIPは展開できたので要らない。stagingだけ残す。
        std::error_code removeError;
        fs::remove(workDirectory / kPackageFileName, removeError);
    } else if (outcome.state == UpdateDownloadState::Failed) {
        std::error_code removeError;
        fs::remove_all(workDirectory, removeError);
    }
    const std::lock_guard<std::mutex> lock(mutex_);
    pending_ = Result{outcome.state, outcome.failure, outcome.state == UpdateDownloadState::Ready ? staging : fs::path{}};
}

bool UpdateDownloader::Poll() {
    std::optional<Result> result;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        result = std::move(pending_);
        pending_.reset();
    }
    if (!result) return false;
    failure_ = result->failure;
    if (result->state == UpdateDownloadState::Ready) {
        stagingDirectory_ = std::move(result->stagingDirectory);
    }
    state_ = result->state;
    return true;
}

void UpdateDownloader::RemoveWorkDirectory() {
    if (workDirectory_.empty()) return;
    std::error_code error;
    fs::remove_all(workDirectory_, error);
    if (error) spdlog::warn("[update] Could not remove the work directory: {}", error.message());
    workDirectory_.clear();
}

void UpdateDownloader::Cancel() {
    const UpdateDownloadState current = state_.load();
    if (current != UpdateDownloadState::Downloading && current != UpdateDownloadState::Verifying &&
        current != UpdateDownloadState::Extracting) {
        return;
    }
    // 先にワーカーを止めて待つ。待たずに消すと、書き込み中のファイルや直前に置かれた結果とぶつかる。
    worker_.request_stop();
    if (worker_.joinable()) worker_.join();
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        pending_.reset();
    }
    RemoveWorkDirectory();
    failure_ = UpdateDownloadFailure::None;
    state_ = UpdateDownloadState::Cancelled;
}

bool UpdateDownloader::DiscardReadyIfVersionDiffers(std::string_view latestVersion) {
    if (state_.load() != UpdateDownloadState::Ready || toVersion_ == latestVersion) return false;
    spdlog::info("[update] Discarding the prepared update: it is not the latest version");
    RemoveWorkDirectory();
    stagingDirectory_.clear();
    toVersion_.clear();
    state_ = UpdateDownloadState::Idle;
    return true;
}

} // namespace rs::app

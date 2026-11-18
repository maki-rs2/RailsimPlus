//
// UpdateDownloader.h - 新しい版のZIPをダウンロードして、stagingへ展開するまでの状態機械。Windows専用。
//
// UpdateCheckerと同じ作りで、公開操作はメインスレッドから呼ぶ。ワーカーは進捗と段階だけをatomicへ書き、
// 最終結果はmutexの下の保留結果に置く。メインスレッドがPollで受け取った時点で Ready / Failed / Cancelled になる。
// 作業フォルダ(<根>/<一意名>)の中は、ダウンロードしたZIPの `package.zip` と、展開先の `staging/` に分ける。
//
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#include "UpdateChecker.h"
#include "../../platform/net/HttpDownload.h"

namespace rs::app {

enum class UpdateDownloadState { Idle, Downloading, Verifying, Extracting, Ready, Failed, Cancelled };

enum class UpdateDownloadFailure {
    None,
    DownloadFailed,
    SizeMismatch,
    HashMismatch,
    ExtractFailed,
    NotEnoughSpace,
    NotWritable,
};

// アプリ内更新を出せるか。出せないときは、その理由。
enum class InAppUpdateAvailability { Available, NoInstallableAsset, NotWritable };

// 規則そのもの(判定の1か所)。資産の情報がそろっていて、インストール先に書き込めるときだけ Available。
InAppUpdateAvailability EvaluateInAppUpdate(const ReleaseInfo& release, bool installDirectoryWritable);

// 作業フォルダの既定の根(%LOCALAPPDATA%\RailsimPlus\update)。
std::filesystem::path DefaultUpdateWorkRoot();

// 根の中身を全部捨てる(前回のstagingは再ダウンロードさせる)。消せなくても進む。GUI起動のときだけ呼ぶ。
void CleanupStaleUpdateDirectories(const std::filesystem::path& workRoot = DefaultUpdateWorkRoot());

// 差し替えられる部品(テストと本番で同じ流れを通すため)。空のものは本番の実装を使う。
struct UpdateDownloaderOptions {
    std::filesystem::path workRoot;
    std::function<rs::platform_net::HttpDownloadResult(const std::string&, const std::filesystem::path&,
                                                       const rs::platform_net::HttpDownloadOptions&)> download;
    std::function<std::optional<std::uint64_t>(const std::filesystem::path&)> queryFreeBytes;
    std::function<bool()> installDirectoryWritable;
};

struct UpdateDownloadProgress {
    std::uint64_t receivedBytes = 0;
    std::uint64_t totalBytes = 0;
};

class UpdateDownloader {
public:
    explicit UpdateDownloader(UpdateDownloaderOptions options = {});
    ~UpdateDownloader();
    UpdateDownloader(const UpdateDownloader&) = delete;
    UpdateDownloader& operator=(const UpdateDownloader&) = delete;

    // アプリ内更新を出せるか。インストール先に書けるかの確認はファイルを作るので、結果を覚えておく
    // (毎フレームの画面更新から呼ばれる)。確認し直すのは、Startのときと InvalidateInstallDirectoryCheck のあと。
    InAppUpdateAvailability Availability(const ReleaseInfo& release) const;
    void InvalidateInstallDirectoryCheck() { installWritableCache_.reset(); }
    bool CanDownloadInApp(const ReleaseInfo& release) const {
        return Availability(release) == InAppUpdateAvailability::Available;
    }

    // Idle / Failed / Cancelled のときだけ始める。始められたら真。
    bool Start(const ReleaseInfo& release);
    // ダウンロード・確認・展開の途中なら、止めて作業フォルダを消し、Cancelled にする。ワーカーの終了を待つ。
    void Cancel();
    // ワーカーの結果をメインスレッドへ取り込む。状態が変わったら真。
    bool Poll();
    // Ready の版が latestVersion と違うなら、stagingを捨てて Idle へ戻す。捨てたら真。
    bool DiscardReadyIfVersionDiffers(std::string_view latestVersion);

    UpdateDownloadState State() const { return state_.load(); }
    UpdateDownloadFailure Failure() const { return failure_; }
    UpdateDownloadProgress Progress() const;
    // 始めるたびに増える番号。同じ試行で通知を二重に出さないために使う。
    std::uint64_t Attempt() const { return attempt_; }
    // Ready のときだけ意味がある。
    const std::filesystem::path& StagingDirectory() const { return stagingDirectory_; }
    const std::string& ToVersion() const { return toVersion_; }

private:
    struct Result {
        UpdateDownloadState state = UpdateDownloadState::Failed;
        UpdateDownloadFailure failure = UpdateDownloadFailure::None;
        std::filesystem::path stagingDirectory;
    };

    void Run(const ReleaseInfo& release, const std::filesystem::path& workDirectory, const std::stop_token& stop);
    void RemoveWorkDirectory();

    UpdateDownloaderOptions options_;
    mutable std::optional<bool> installWritableCache_;
    std::atomic<UpdateDownloadState> state_{UpdateDownloadState::Idle};
    std::atomic<std::uint64_t> receivedBytes_{0};
    std::atomic<std::uint64_t> totalBytes_{0};
    UpdateDownloadFailure failure_ = UpdateDownloadFailure::None;
    std::uint64_t attempt_ = 0;
    std::filesystem::path workDirectory_;
    std::filesystem::path stagingDirectory_;
    std::string toVersion_;
    std::mutex mutex_;
    std::optional<Result> pending_;
    std::jthread worker_;
};

} // namespace rs::app

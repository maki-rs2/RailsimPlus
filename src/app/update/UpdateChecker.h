#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace rs::app {
struct UserSettingsData;

struct UpdateCheckConfig {
    std::string releaseApiUrl;
    std::string releasePageUrl;
    static UpdateCheckConfig FromJson(std::string_view json);
    static const UpdateCheckConfig& Embedded();
};

// リリースの資産(配布ZIP)の名前。
inline constexpr std::string_view kReleaseAssetName = "RailsimPlus-win64.zip";

struct ReleaseInfo {
    std::string version;
    std::string htmlUrl;
    // 配布ZIPの情報。URL・サイズ・ハッシュが3つそろったときだけ入り、1つでも欠ければ3つとも空(0)。
    std::string assetUrl;
    std::uint64_t assetSize = 0;
    std::string assetSha256; // 小文字16進64桁

    // アプリ内更新に使える資産があるか。
    bool HasInstallableAsset() const { return !assetUrl.empty() && assetSize > 0 && !assetSha256.empty(); }
};

bool IsNewerVersion(std::string_view current, std::string_view candidate);
// アプリ内でダウンロードしてよいURLか。このリポジトリのリリース資産のURLだけを許す。
bool IsAllowedDownloadUrl(std::string_view url);
std::optional<ReleaseInfo> ParseReleaseInfo(std::string_view json);
bool IsUpdateCheckDue(std::int64_t lastCheckTime, std::int64_t now);

enum class UpdateCheckState { Unchecked, Checking, Current, Available, Failed };
std::string_view UpdateCheckStateTextKey(UpdateCheckState state);

// 公開操作はメインスレッドから呼ぶ。ワーカーは保留結果だけをmutexの下で書く。
class UpdateChecker {
public:
    explicit UpdateChecker(UpdateCheckConfig config = UpdateCheckConfig::Embedded());
    ~UpdateChecker();
    bool Request(UserSettingsData& settings, bool manual = false);
    bool Poll();
    bool CanCheck() const;
    UpdateCheckState State() const { return state_; }
    const ReleaseInfo& Release() const { return release_; }
    std::string DownloadUrl() const;
    void OpenDownloadPage() const;

private:
    struct Result {
        UpdateCheckState state = UpdateCheckState::Failed;
        ReleaseInfo release;
    };
    UpdateCheckConfig config_;
    UpdateCheckState state_ = UpdateCheckState::Unchecked;
    ReleaseInfo release_;
    std::mutex mutex_;
    std::optional<Result> pending_;
    std::jthread worker_;
};
} // namespace rs::app

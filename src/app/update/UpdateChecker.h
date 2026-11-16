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

struct ReleaseInfo {
    std::string version;
    std::string htmlUrl;
};

bool IsNewerVersion(std::string_view current, std::string_view candidate);
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

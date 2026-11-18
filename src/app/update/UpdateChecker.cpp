#include "UpdateChecker.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <chrono>
#include <utility>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "EmbeddedUpdateCheck.h"
#include "../../core/ConfigJson.h"
#include "../../platform/text/AsciiCase.h"
#include "../settings/UserSettings.h"
#ifdef _WIN32
#include "../../platform/net/HttpGet.h"
#endif

namespace rs::app {
namespace {
std::optional<std::array<unsigned int, 3>> ParseVersion(std::string_view text) {
    if (text.starts_with('v')) text.remove_prefix(1);
    std::array<unsigned int, 3> parts{};
    for (std::size_t index = 0; index < parts.size(); ++index) {
        const std::size_t end = text.find('.');
        const std::string_view part = text.substr(0, end);
        if (part.empty()) return std::nullopt;
        const auto [ptr, error] = std::from_chars(part.data(), part.data() + part.size(), parts[index]);
        if (error != std::errc{} || ptr != part.data() + part.size()) return std::nullopt;
        if (index + 1 == parts.size()) {
            if (end != std::string_view::npos) return std::nullopt;
        } else {
            if (end == std::string_view::npos) return std::nullopt;
            text.remove_prefix(end + 1);
        }
    }
    return parts;
}

// assets[] から配布ZIPを1件探して、URL・サイズ・ハッシュを読む。1つでも不正なら何も入れない。
void ReadReleaseAsset(const nlohmann::json& root, ReleaseInfo& info) {
    constexpr std::string_view kDigestPrefix = "sha256:";
    constexpr std::size_t kSha256HexLength = 64;
    const auto assets = root.find("assets");
    if (assets == root.end() || !assets->is_array()) return;
    for (const nlohmann::json& asset : *assets) {
        if (!asset.is_object()) continue;
        const auto name = asset.find("name");
        if (name == asset.end() || !name->is_string() || name->get<std::string>() != kReleaseAssetName) continue;
        const auto url = asset.find("browser_download_url");
        const auto size = asset.find("size");
        const auto digest = asset.find("digest");
        if (url == asset.end() || !url->is_string() || !IsAllowedDownloadUrl(url->get<std::string>())) return;
        if (size == asset.end() || !size->is_number_unsigned() || size->get<std::uint64_t>() == 0) return;
        if (digest == asset.end() || !digest->is_string()) return;
        std::string hash = digest->get<std::string>();
        if (!hash.starts_with(kDigestPrefix)) return;
        hash.erase(0, kDigestPrefix.size());
        const bool isHex = std::all_of(hash.begin(), hash.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
        if (hash.size() != kSha256HexLength || !isHex) return;
        info.assetUrl = url->get<std::string>();
        info.assetSize = size->get<std::uint64_t>();
        info.assetSha256 = rs::platform_text::ToAsciiLower(std::move(hash));
        return;
    }
}

UpdateCheckConfig ReadConfig(const nlohmann::json& root) {
    return {root.value("releaseApiUrl", std::string{}), root.value("releasePageUrl", std::string{})};
}
} // namespace

UpdateCheckConfig UpdateCheckConfig::FromJson(std::string_view json) {
    return ReadConfig(nlohmann::json::parse(json));
}

const UpdateCheckConfig& UpdateCheckConfig::Embedded() {
    static const UpdateCheckConfig kConfig = rs::core::ReadEmbeddedConfig<UpdateCheckConfig>(
        rs::embedded::kUpdateCheckJson, rs::embedded::kUpdateCheckJsonSize, "UpdateCheck", ReadConfig);
    return kConfig;
}

bool IsAllowedDownloadUrl(std::string_view url) {
    constexpr std::string_view kAllowedPrefix = "https://github.com/maki-rs2/RailsimPlus/releases/download/";
    // 接頭辞の後ろに資産のパスがあること。空白・制御文字・バックスラッシュ・フラグメント・`..` の要素は、
    // 接頭辞の外(別のリポジトリなど)を指せてしまうので許さない。
    if (!url.starts_with(kAllowedPrefix) || url.size() == kAllowedPrefix.size()) return false;
    if (url.find("/..") != std::string_view::npos) return false;
    return std::none_of(url.begin(), url.end(), [](char c) {
        return static_cast<unsigned char>(c) <= 0x20 || c == '\\' || c == '#';
    });
}

bool IsNewerVersion(std::string_view current, std::string_view candidate) {
    const std::optional<std::array<unsigned int, 3>> currentParts = ParseVersion(current);
    const std::optional<std::array<unsigned int, 3>> candidateParts = ParseVersion(candidate);
    return currentParts && candidateParts && *currentParts < *candidateParts;
}

std::optional<ReleaseInfo> ParseReleaseInfo(std::string_view json) {
    const nlohmann::json root = nlohmann::json::parse(json, nullptr, false);
    if (!root.is_object()) return std::nullopt;
    const auto tag = root.find("tag_name");
    const auto url = root.find("html_url");
    if (tag == root.end() || !tag->is_string() || !ParseVersion(tag->get<std::string>()) ||
        url == root.end() || !url->is_string()) return std::nullopt;
    std::string version = tag->get<std::string>();
    if (version.starts_with('v')) version.erase(0, 1);
    ReleaseInfo info{std::move(version), url->get<std::string>(), {}, 0, {}};
    ReadReleaseAsset(root, info);
    return info;
}

bool IsUpdateCheckDue(std::int64_t lastCheckTime, std::int64_t now) {
    // 自動確認の間隔は24時間(UTCのepoch秒)。時計が戻ったときも連続で確認しない。
    constexpr std::int64_t kCheckIntervalSeconds = std::int64_t{24} * 60 * 60;
    return lastCheckTime <= 0 || (now >= lastCheckTime && now - lastCheckTime >= kCheckIntervalSeconds);
}

std::string_view UpdateCheckStateTextKey(UpdateCheckState state) {
    constexpr std::array<const char*, 5> kKeys{"game_settings.about.unchecked", "game_settings.about.checking",
                               "game_settings.about.current", "game_settings.about.available",
                               "game_settings.about.failed"};
    static_assert(kKeys.size() == static_cast<std::size_t>(UpdateCheckState::Failed) + 1);
    return kKeys[static_cast<std::size_t>(state)];
}

UpdateChecker::UpdateChecker(UpdateCheckConfig config) : config_(std::move(config)) {}
UpdateChecker::~UpdateChecker() = default;

bool UpdateChecker::CanCheck() const {
#ifdef _WIN32
    return rs::platform_net::IsHttpsUrl(config_.releaseApiUrl) && state_ != UpdateCheckState::Checking;
#else
    return false;
#endif
}

bool UpdateChecker::Request(UserSettingsData& settings, bool manual) {
    if (!CanCheck()) return false;
    const std::int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (!manual && (!settings.updateCheckEnabled || !IsUpdateCheckDue(settings.lastUpdateCheckTime, now))) {
        return false;
    }
#ifdef _WIN32
    try {
        worker_ = std::jthread([this] {
            Result result;
            try {
                const std::optional<std::string> body = rs::platform_net::HttpGet(
                    config_.releaseApiUrl, std::string("RailsimPlus/") + RS_APP_VERSION);
                const std::optional<ReleaseInfo> info = body ? ParseReleaseInfo(*body) : std::nullopt;
                if (info) {
                    result.release = *info;
                    result.state = IsNewerVersion(RS_APP_VERSION, info->version)
                                       ? UpdateCheckState::Available : UpdateCheckState::Current;
                }
            } catch (const std::exception& error) {
                spdlog::warn("[update] Check failed: {}", error.what());
            }
            if (result.state == UpdateCheckState::Failed) spdlog::warn("[update] Could not check releases");
            const std::lock_guard<std::mutex> lock(mutex_);
            pending_ = std::move(result);
        });
    } catch (const std::exception& error) {
        spdlog::warn("[update] Could not start check: {}", error.what());
        state_ = UpdateCheckState::Failed;
        return false;
    }
    state_ = UpdateCheckState::Checking;
    settings.lastUpdateCheckTime = now;
    SaveUserSettingsOrWarn(settings, "Failed to save update check time.");
    return true;
#else
    return false;
#endif
}

bool UpdateChecker::Poll() {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (!pending_) return false;
    state_ = pending_->state;
    release_ = std::move(pending_->release);
    pending_.reset();
    return true;
}

std::string UpdateChecker::DownloadUrl() const {
    return config_.releasePageUrl.empty() ? release_.htmlUrl : config_.releasePageUrl;
}

void UpdateChecker::OpenDownloadPage() const {
#ifdef _WIN32
    if (state_ == UpdateCheckState::Available && !rs::platform_net::OpenHttpsUrl(DownloadUrl())) {
        spdlog::warn("[update] Could not open download page");
    }
#endif
}
} // namespace rs::app

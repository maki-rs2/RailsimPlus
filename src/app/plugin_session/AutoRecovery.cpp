#include "AutoRecovery.h"

#include <windows.h>

#include <fstream>
#include <string>
#include <optional>

#include <nlohmann/json.hpp>

#include "../../platform/file/AtomicFileWrite.h"
#include "../../platform/file/UserDataDirectory.h"
#include "../../platform/text/Encoding.h"
#include "../../world/alignment/GuidJson.h"

namespace rs::app {
namespace {

constexpr wchar_t kAutoRecoveryDirectoryName[] = L"autorecover";
constexpr wchar_t kSnapshotFileName[] = L"layout.rsl";
constexpr wchar_t kSourceFileName[] = L"source.json";
constexpr wchar_t kCrashMarkerFileName[] = L"crash.marker";

std::filesystem::path CrashMarkerPath(const std::filesystem::path& root) {
    return root / kCrashMarkerFileName;
}

// 読めなければ何も入れない(候補は名前なしで残す)。
void ReadSourceInto(const std::filesystem::path& sourcePath, AutoRecoveryCandidate& candidate) {
    std::ifstream stream(sourcePath, std::ios::binary);
    if (!stream) return;
    const nlohmann::json document = nlohmann::json::parse(stream, nullptr, false);
    if (!document.is_object()) return;
    candidate.sourcePathUtf8 = document.value("path", std::string{});
    candidate.displayNameUtf8 = document.value("name", std::string{});
}

}  // namespace

std::filesystem::path DefaultAutoRecoveryRoot() {
    return rs::platform_file::UserDataDirectory() / kAutoRecoveryDirectoryName;
}

std::filesystem::path AutoRecoverySnapshotPath(const std::filesystem::path& root,
                                               const rs::world::Guid& guid) {
    return root / rs::platform_text::Utf8ToWide(rs::world::GuidToHex(guid)) / kSnapshotFileName;
}

bool ShouldWriteAutoRecovery(const AutoRecoverySchedule& schedule, bool dirty,
                             bool editInProgress, double nowSeconds) {
    return schedule.enabled && dirty && !editInProgress &&
           nowSeconds - schedule.lastAttemptSeconds >= static_cast<double>(schedule.intervalSeconds);
}

bool ClearAutoRecovery(const std::filesystem::path& root, const rs::world::Guid& guid) {
    std::error_code error;
    std::filesystem::remove_all(root / rs::platform_text::Utf8ToWide(rs::world::GuidToHex(guid)), error);
    return !error;
}

std::vector<AutoRecoveryCandidate> FindAutoRecoveryCandidates(const std::filesystem::path& root) {
    std::vector<AutoRecoveryCandidate> candidates;
    std::error_code error;
    if (!std::filesystem::is_directory(root, error) || error) return candidates;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(root, error)) {
        if (error || !entry.is_directory(error)) continue;
        const std::optional<rs::world::Guid> guid =
            rs::world::GuidFromHex(rs::platform_text::WideToUtf8(entry.path().filename().wstring()));
        const std::filesystem::path snapshotPath = entry.path() / kSnapshotFileName;
        if (guid && std::filesystem::is_regular_file(snapshotPath, error) && !error) {
            AutoRecoveryCandidate candidate;
            candidate.guid = *guid;
            candidate.snapshotPath = snapshotPath;
            candidate.writeTime = std::filesystem::last_write_time(snapshotPath, error);
            ReadSourceInto(entry.path() / kSourceFileName, candidate);
            candidates.push_back(std::move(candidate));
        }
        error.clear();
    }
    return candidates;
}

LayoutActionResult WriteAutoRecoverySource(const std::filesystem::path& root, const rs::world::Guid& guid,
                             const std::string& sourcePathUtf8, const std::string& displayNameUtf8) {
    const std::filesystem::path sourcePath = AutoRecoverySnapshotPath(root, guid).parent_path() / kSourceFileName;
    const nlohmann::json document = {{"path", sourcePathUtf8}, {"name", displayNameUtf8}};
    std::string writeError;
    if (!rs::platform_file::WriteFileAtomically(sourcePath, document.dump(), writeError)) {
        return LayoutActionResult::Failure(LayoutActionStatus::FileWriteFailed,
                                            rs::platform_text::WideToUtf8(sourcePath.wstring()), writeError);
    }
    return {};
}

bool WriteCrashMarker(const std::filesystem::path& root) {
    std::error_code error;
    std::filesystem::create_directories(root, error);
    if (error) return false;
    std::string writeError;
    return rs::platform_file::WriteFileAtomically(CrashMarkerPath(root), "running\n", writeError);
}

bool ClearCrashMarker(const std::filesystem::path& root) {
    std::error_code error;
    std::filesystem::remove(CrashMarkerPath(root), error);
    return !error;
}

bool HasCrashMarker(const std::filesystem::path& root) {
    std::error_code error;
    return std::filesystem::is_regular_file(CrashMarkerPath(root), error) && !error;
}

}  // namespace rs::app

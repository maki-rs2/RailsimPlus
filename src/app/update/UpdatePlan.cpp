#include "UpdatePlan.h"

#include <algorithm>
#include <set>

#include "../../platform/file/ZipArchive.h"
#include "../../platform/text/AsciiCase.h"

namespace rs::app {
namespace {
std::string CaseFoldKey(std::string_view path) { return rs::platform_text::ToAsciiLower(std::string(path)); }

bool EndsWithIgnoreCase(std::string_view text, std::string_view suffix) {
    return text.size() >= suffix.size() && CaseFoldKey(text.substr(text.size() - suffix.size())) == suffix;
}

std::string_view FileName(std::string_view path) {
    const std::size_t slash = path.rfind('/');
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

// 実行ファイルを最後にする。順序の決まりはここだけ。
void PutExecutableLast(std::vector<UpdateOperation>& operations) {
    const std::string executableKey = CaseFoldKey(kExecutableRelativePath);
    std::stable_partition(operations.begin(), operations.end(), [&](const UpdateOperation& operation) {
        return CaseFoldKey(operation.path) != executableKey;
    });
}

std::vector<std::string> ParentDirectories(const std::vector<UpdateOperation>& operations) {
    std::set<std::string> directories;
    for (const UpdateOperation& operation : operations) {
        if (operation.kind != UpdateOperationKind::Add) continue;
        for (std::size_t slash = operation.path.find('/'); slash != std::string::npos;
             slash = operation.path.find('/', slash + 1)) {
            directories.insert(operation.path.substr(0, slash));
        }
    }
    // 親から先に作れるよう、階層の浅い順に並べる。
    std::vector<std::string> sorted(directories.begin(), directories.end());
    std::stable_sort(sorted.begin(), sorted.end(), [](const std::string& left, const std::string& right) {
        return std::count(left.begin(), left.end(), '/') < std::count(right.begin(), right.end(), '/');
    });
    return sorted;
}
} // namespace

bool IsSafeRelativePath(std::string_view relativePath) {
    if (relativePath.empty() || relativePath.back() == '/') return false;
    return rs::platform_file::IsSafeArchiveEntryPath(std::string(rs::platform_file::kArchiveRootPrefix) +
                                                     std::string(relativePath));
}

bool IsUpdateOwnFile(std::string_view relativePath) {
    if (CaseFoldKey(FileName(relativePath)) == CaseFoldKey(kUpdateMarkerFileName)) return true;
    return EndsWithIgnoreCase(relativePath, ".bak") || EndsWithIgnoreCase(relativePath, ".new") ||
           EndsWithIgnoreCase(relativePath, ".failed");
}

std::vector<std::string> ParseInstalledFilesList(std::string_view text) {
    std::vector<std::string> files;
    while (!text.empty()) {
        const std::size_t end = text.find('\n');
        std::string_view line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.remove_suffix(1);
        if (line.starts_with(rs::platform_file::kArchiveRootPrefix)) line.remove_prefix(rs::platform_file::kArchiveRootPrefix.size());
        if (IsSafeRelativePath(line)) files.emplace_back(line);
    }
    return files;
}

UpdatePlan BuildUpdatePlan(const std::vector<std::string>& stagedFiles, std::string_view previousInstalledFiles,
                           const FileExistsFn& existsInInstall) {
    UpdatePlan plan;
    std::set<std::string> stagedKeys;
    for (const std::string& file : stagedFiles) {
        if (!IsSafeRelativePath(file)) continue;
        // installed-files.txt は更新機能の管理するファイルだが、次回の Delete の元になるので、ふつうの1ファイルとして置く。
        if (IsUpdateOwnFile(file) || !stagedKeys.insert(CaseFoldKey(file)).second) continue;
        plan.operations.push_back({file, existsInInstall(file) ? UpdateOperationKind::Replace : UpdateOperationKind::Add});
    }
    // 新しい一覧(staging)に無くなったファイルだけ、実際に今あれば消す。
    const std::string executableKey = CaseFoldKey(kExecutableRelativePath);
    for (const std::string& file : ParseInstalledFilesList(previousInstalledFiles)) {
        const std::string key = CaseFoldKey(file);
        if (IsUpdateOwnFile(file) || key == executableKey || key == CaseFoldKey(kInstalledFilesName)) continue;
        if (!stagedKeys.insert(key).second || !existsInInstall(file)) continue;
        plan.operations.push_back({file, UpdateOperationKind::Delete});
    }
    PutExecutableLast(plan.operations);
    plan.directoriesToCreate = ParentDirectories(plan.operations);
    return plan;
}

bool HasEnoughDiskSpace(std::uint64_t requiredBytes, std::uint64_t availableBytes) {
    return availableBytes >= requiredBytes;
}

} // namespace rs::app

#include "LayoutFileCatalog.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <iterator>
#include <string_view>
#include <system_error>

#include "../../platform/text/AsciiCase.h"
#include "../../platform/text/Encoding.h"

namespace rs::app {
namespace {

constexpr std::string_view kLayoutExtension = ".rsl";
constexpr std::string_view kRs2Extension = ".rs2";
constexpr wchar_t kRs2LayoutFolderName[] = L"Layout";
constexpr std::string_view kInvalidNameCharacters = "\\/:*?\"<>|";

std::filesystem::path ToPath(const std::string& pathUtf8) {
    return std::filesystem::path(rs::platform_text::Utf8ToWide(pathUtf8));
}

std::string ToUtf8(const std::filesystem::path& path) {
    return rs::platform_text::WideToUtf8(path.wstring());
}

LayoutFileEntry MakeEntry(const std::filesystem::path& path, std::filesystem::file_time_type writeTime) {
    LayoutFileEntry entry;
    entry.pathUtf8 = ToUtf8(path);
    entry.nameUtf8 = rs::platform_text::WideToUtf8(path.stem().wstring());
    entry.dateUtf8 = FormatLayoutFileTime(writeTime);
    entry.writeTime = writeTime;
    return entry;
}

// 同じファイルかを比べるための形。区切りをそろえ、大文字小文字を無視する。
std::string ComparablePath(const std::string& pathUtf8) {
    std::string comparable = rs::platform_text::ToAsciiLower(pathUtf8);
    std::replace(comparable.begin(), comparable.end(), '\\', '/');
    return comparable;
}

std::string LowerExtensionUtf8(const std::filesystem::path& path) {
    return rs::platform_text::ToAsciiLower(rs::platform_text::WideToUtf8(path.extension().wstring()));
}

bool IsExistingFile(const std::string& pathUtf8) {
    std::error_code error;
    return std::filesystem::is_regular_file(ToPath(pathUtf8), error) && !error;
}

} // namespace

std::vector<LayoutFileEntry> ListLayoutFolder(const std::filesystem::path& directory) {
    std::vector<LayoutFileEntry> entries;
    std::error_code error;
    std::filesystem::directory_iterator iterator(directory, error);
    if (error) return entries;
    for (const std::filesystem::directory_entry& item : iterator) {
        std::error_code itemError;
        if (!item.is_regular_file(itemError) || itemError) continue;
        if (LowerExtensionUtf8(item.path()) != kLayoutExtension) continue;
        const std::filesystem::file_time_type writeTime = item.last_write_time(itemError);
        if (itemError) continue;
        entries.push_back(MakeEntry(item.path(), writeTime));
    }
    std::stable_sort(entries.begin(), entries.end(), [](const LayoutFileEntry& left, const LayoutFileEntry& right) {
        return left.writeTime > right.writeTime;
    });
    return entries;
}

std::vector<LayoutFileEntry> ListRs2Layouts(const std::vector<std::string>& pluginRoots) {
    std::vector<LayoutFileEntry> entries;
    for (const std::string& rootUtf8 : pluginRoots) {
        std::error_code error;
        std::filesystem::directory_iterator iterator(ToPath(rootUtf8) / kRs2LayoutFolderName, error);
        if (error) continue;
        std::vector<LayoutFileEntry> inRoot;
        for (const std::filesystem::directory_entry& item : iterator) {
            std::error_code itemError;
            if (!item.is_regular_file(itemError) || itemError) continue;
            if (LowerExtensionUtf8(item.path()) != kRs2Extension) continue;
            const std::filesystem::file_time_type writeTime = item.last_write_time(itemError);
            if (itemError) continue;
            inRoot.push_back(MakeEntry(item.path(), writeTime));
        }
        std::sort(inRoot.begin(), inRoot.end(), [](const LayoutFileEntry& left, const LayoutFileEntry& right) {
            return left.nameUtf8 < right.nameUtf8;
        });
        entries.insert(entries.end(), std::make_move_iterator(inRoot.begin()), std::make_move_iterator(inRoot.end()));
    }
    return entries;
}

bool IsRs2Path(const std::string& pathUtf8) {
    return LowerExtensionUtf8(ToPath(pathUtf8)) == kRs2Extension;
}

std::vector<LayoutFileEntry> ListExistingLayouts(const std::vector<std::string>& paths) {
    std::vector<LayoutFileEntry> entries;
    for (const std::string& pathUtf8 : paths) {
        if (!IsExistingFile(pathUtf8)) continue;
        const std::filesystem::path path = ToPath(pathUtf8);
        std::error_code error;
        const std::filesystem::file_time_type writeTime = std::filesystem::last_write_time(path, error);
        if (!error) entries.push_back(MakeEntry(path, writeTime));
    }
    return entries;
}

void AddRecentLayout(std::vector<std::string>& recentPaths, const std::string& pathUtf8) {
    const std::string comparable = ComparablePath(pathUtf8);
    std::erase_if(recentPaths, [&comparable](const std::string& path) { return ComparablePath(path) == comparable; });
    recentPaths.insert(recentPaths.begin(), pathUtf8);
    if (recentPaths.size() > kMaxRecentLayouts) recentPaths.resize(kMaxRecentLayouts);
}

bool RemoveMissingRecentLayouts(std::vector<std::string>& recentPaths) {
    return std::erase_if(recentPaths, [](const std::string& path) { return !IsExistingFile(path); }) > 0;
}

bool IsValidLayoutFileName(const std::string& nameUtf8) {
    if (nameUtf8.find_first_of(kInvalidNameCharacters) != std::string::npos) return false;
    return nameUtf8.find_first_not_of(" \t") != std::string::npos;
}

std::string LayoutNameFromPath(const std::string& pathUtf8) {
    return rs::platform_text::WideToUtf8(ToPath(pathUtf8).stem().wstring());
}

std::string FormatLayoutFileTime(std::filesystem::file_time_type time) {
    const std::chrono::system_clock::time_point systemTime =
        std::chrono::clock_cast<std::chrono::system_clock>(time);
    const std::time_t seconds = std::chrono::system_clock::to_time_t(systemTime);
    std::tm local = {};
#ifdef _WIN32
    localtime_s(&local, &seconds);
#else
    localtime_r(&seconds, &local);
#endif
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%04d/%02d/%02d %02d:%02d", local.tm_year + 1900, local.tm_mon + 1,
                  local.tm_mday, local.tm_hour, local.tm_min);
    return buffer;
}

} // namespace rs::app

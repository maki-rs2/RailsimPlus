#include "ZipArchive.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <unordered_set>

#include <miniz.h>

#include "../text/Encoding.h"

namespace rs::platform_file {
namespace {

// 接頭辞を除いた相対パスの長さの上限。展開先のフォルダの深さを足しても、Windowsのパス長の上限(260)に収める。
constexpr std::size_t kMaxRelativePathLength = 200;

// ZIPの「作成元」(version made by の上位バイト)。
constexpr mz_uint16 kHostDos = 0;
constexpr mz_uint16 kHostUnix = 3;
constexpr mz_uint16 kHostNtfs = 10;
constexpr mz_uint32 kUnixFileTypeMask = 0170000;
constexpr mz_uint32 kUnixSymlink = 0120000;
constexpr mz_uint32 kNtfsReparsePoint = 0x400;

// Windowsが予約しているデバイス名(拡張子が付いても予約のまま)。
constexpr std::array<std::string_view, 22> kReservedNames{
    "CON",  "PRN",  "AUX",  "NUL",  "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7",
    "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};

char AsciiUpper(char c) { return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c; }

bool IsReservedName(std::string_view component) {
    std::string stem(component.substr(0, component.find('.')));
    std::transform(stem.begin(), stem.end(), stem.begin(), AsciiUpper);
    return std::find(kReservedNames.begin(), kReservedNames.end(), stem) != kReservedNames.end();
}

bool IsSafeComponent(std::string_view component) {
    if (component.empty() || component == "." || component == "..") return false;
    if (component.back() == '.' || component.back() == ' ') return false;
    // `\` と `:` は区切り・ドライブ文字・代替データストリームになる。そのほかはNTFSの名前に使えない文字。
    for (const char c : component) {
        if (static_cast<unsigned char>(c) < 0x20 || std::string_view("\\:<>\"|?*").find(c) != std::string_view::npos) {
            return false;
        }
    }
    return !IsReservedName(component);
}

std::string CaseFoldedPath(std::string_view entryName) {
    std::string folded(ArchiveRelativePath(entryName));
    std::transform(folded.begin(), folded.end(), folded.begin(),
                   [](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; });
    return folded;
}

struct ZipFile {
    std::FILE* file = nullptr;
    mz_zip_archive archive{};
    bool readerOpen = false;
    ~ZipFile() {
        if (readerOpen) mz_zip_reader_end(&archive);
        if (file != nullptr) std::fclose(file);
    }
    bool Open(const std::filesystem::path& path, std::string& outError) {
        if (_wfopen_s(&file, path.c_str(), L"rb") != 0 || file == nullptr) {
            file = nullptr;
            outError = "cannot open archive";
            return false;
        }
        mz_zip_zero_struct(&archive);
        if (!mz_zip_reader_init_cfile(&archive, file, 0, 0)) {
            outError = std::string("invalid archive: ") + mz_zip_get_error_string(mz_zip_get_last_error(&archive));
            return false;
        }
        readerOpen = true;
        return true;
    }
};

std::vector<ZipEntryInfo> ReadEntries(ZipFile& zip, std::string& outError) {
    std::vector<ZipEntryInfo> entries;
    const mz_uint count = mz_zip_reader_get_num_files(&zip.archive);
    entries.reserve(count);
    for (mz_uint index = 0; index < count; ++index) {
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&zip.archive, index, &stat)) {
            outError = "cannot read entry";
            return {};
        }
        const mz_uint16 host = static_cast<mz_uint16>(stat.m_version_made_by >> 8);
        ZipEntryInfo info;
        info.name = stat.m_filename;
        info.uncompressedSize = stat.m_uncomp_size;
        info.isDirectory = stat.m_is_directory != 0;
        info.isEncrypted = stat.m_is_encrypted != 0 || stat.m_is_supported == 0;
        info.isSymlink = (host == kHostUnix && ((stat.m_external_attr >> 16) & kUnixFileTypeMask) == kUnixSymlink) ||
                         ((host == kHostDos || host == kHostNtfs) && (stat.m_external_attr & kNtfsReparsePoint) != 0);
        entries.push_back(std::move(info));
    }
    return entries;
}

// 展開前の検査。1つでも不正なら書き込みを始めない。
bool ValidateEntries(const std::vector<ZipEntryInfo>& entries, const ZipLimits& limits, std::string& outError) {
    if (entries.size() > limits.maxEntryCount) {
        outError = "too many entries";
        return false;
    }
    std::vector<std::string> names;
    names.reserve(entries.size());
    std::uint64_t total = 0;
    for (const ZipEntryInfo& entry : entries) {
        if (!IsSafeArchiveEntryPath(entry.name)) {
            outError = "unsafe entry path: " + entry.name;
            return false;
        }
        if (entry.isSymlink || entry.isEncrypted) {
            outError = "unsupported entry (symlink, encrypted, or unknown method): " + entry.name;
            return false;
        }
        if (entry.uncompressedSize > limits.maxTotalUncompressedBytes - std::min(total, limits.maxTotalUncompressedBytes)) {
            outError = "uncompressed size limit exceeded";
            return false;
        }
        total += entry.uncompressedSize;
        names.push_back(entry.name);
    }
    if (HasDuplicateArchivePath(names)) {
        outError = "duplicate entry path";
        return false;
    }
    return true;
}

std::filesystem::path DestinationPath(const std::filesystem::path& destination, const std::string& entryName) {
    return destination / std::filesystem::path(rs::platform_text::Utf8ToWide(ArchiveRelativePath(entryName)));
}

std::size_t WriteToStream(void* opaque, mz_uint64 /*offset*/, const void* buffer, std::size_t size) {
    std::ofstream& stream = *static_cast<std::ofstream*>(opaque);
    stream.write(static_cast<const char*>(buffer), static_cast<std::streamsize>(size));
    return stream ? size : 0;
}

}  // namespace

bool IsSafeArchiveEntryPath(std::string_view entryName) {
    if (!entryName.starts_with(kArchiveRootPrefix)) return false;
    std::string_view relative = entryName.substr(kArchiveRootPrefix.size());
    if (relative.empty()) return true;
    if (relative.back() == '/') relative.remove_suffix(1);
    if (relative.empty() || relative.size() > kMaxRelativePathLength) return false;
    for (;;) {
        const std::size_t slash = relative.find('/');
        if (!IsSafeComponent(relative.substr(0, slash))) return false;
        if (slash == std::string_view::npos) return true;
        relative.remove_prefix(slash + 1);
    }
}

std::string ArchiveRelativePath(std::string_view entryName) {
    std::string_view relative = entryName;
    if (relative.starts_with(kArchiveRootPrefix)) relative.remove_prefix(kArchiveRootPrefix.size());
    if (!relative.empty() && relative.back() == '/') relative.remove_suffix(1);
    return std::string(relative);
}

bool HasDuplicateArchivePath(const std::vector<std::string>& entryNames) {
    std::unordered_set<std::string> seen;
    for (const std::string& name : entryNames) {
        if (!seen.insert(CaseFoldedPath(name)).second) return true;
    }
    return false;
}

std::vector<ZipEntryInfo> ListZipEntries(const std::filesystem::path& zipPath, std::string& outError) {
    outError.clear();
    ZipFile zip;
    if (!zip.Open(zipPath, outError)) return {};
    return ReadEntries(zip, outError);
}

bool ExtractZip(const std::filesystem::path& zipPath, const std::filesystem::path& destination,
                const ZipLimits& limits, std::string& outError) {
    outError.clear();
    ZipFile zip;
    if (!zip.Open(zipPath, outError)) return false;
    const std::vector<ZipEntryInfo> entries = ReadEntries(zip, outError);
    if (!outError.empty() || !ValidateEntries(entries, limits, outError)) return false;

    std::error_code error;
    std::filesystem::create_directories(destination, error);
    if (error) {
        outError = "cannot create destination";
        return false;
    }
    for (std::size_t index = 0; index < entries.size(); ++index) {
        const std::filesystem::path path = DestinationPath(destination, entries[index].name);
        if (ArchiveRelativePath(entries[index].name).empty()) continue;
        if (entries[index].isDirectory) {
            std::filesystem::create_directories(path, error);
            if (error) {
                outError = "cannot create directory: " + entries[index].name;
                return false;
            }
            continue;
        }
        std::filesystem::create_directories(path.parent_path(), error);
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (error || !stream.is_open() ||
            !mz_zip_reader_extract_to_callback(&zip.archive, static_cast<mz_uint>(index), WriteToStream, &stream, 0)) {
            outError = "cannot extract entry: " + entries[index].name;
            return false;
        }
        stream.close();
        if (!stream) {
            outError = "cannot write entry: " + entries[index].name;
            return false;
        }
    }
    return true;
}

}  // namespace rs::platform_file

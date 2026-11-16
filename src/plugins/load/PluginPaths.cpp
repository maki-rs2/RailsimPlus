//
// PluginPaths.cpp - PluginPaths.h の実装（PluginLoader.cpp から分離）
//
#include "PluginPaths.h"

#include <filesystem>
#include "../../platform/text/AsciiCase.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <wchar.h>

#include <algorithm>
#include <filesystem>
#include <system_error>
#endif

#include "../../platform/text/Encoding.h"

namespace rs::legacy_plugin {

std::string NormalizePluginFolderKey(const std::string& folderUtf8) {
    if (folderUtf8.empty()) return {};
    const std::filesystem::path path(NormalizeSlashes(rs::platform_text::Utf8ToWide(folderUtf8)));
    std::error_code error;
    std::filesystem::path absolute = std::filesystem::absolute(path, error);
    if (error) absolute = path;
    std::filesystem::path normalized = std::filesystem::weakly_canonical(absolute, error);
    if (error) normalized = absolute.lexically_normal();
    while (normalized != normalized.root_path() && normalized.filename().empty())
        normalized = normalized.parent_path();
    return rs::platform_text::ToAsciiLower(rs::platform_text::WideToUtf8(normalized.generic_wstring()));
}

namespace {

// このOSのパス区切り。プラグインの定義はバックスラッシュ区切りで書かれているので、
// Windows以外では読み込み時にこの区切りへ寄せる。
#ifdef _WIN32
constexpr wchar_t kSeparator = L'\\';
constexpr wchar_t kForeignSeparator = L'/';
#else
constexpr wchar_t kSeparator = L'/';
constexpr wchar_t kForeignSeparator = L'\\';
#endif

// 大文字小文字を区別しない比較(先頭n文字)。
int CompareNoCaseN(const wchar_t* a, const wchar_t* b, size_t n) {
#ifdef _WIN32
    return _wcsnicmp(a, b, n);
#else
    return ::wcsncasecmp(a, b, n);
#endif
}

} // namespace

int CompareNoCase(const wchar_t* a, const wchar_t* b) {
#ifdef _WIN32
    return _wcsicmp(a, b);
#else
    return ::wcscasecmp(a, b);
#endif
}

#ifdef _WIN32
bool FileExists(const std::wstring& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
#else
bool FileExists(const std::wstring& path) {
    std::error_code error;
    return std::filesystem::is_regular_file(std::filesystem::path(path), error);
}
#endif

std::string FindPairedTexture(const std::wstring& resolvedBase, const wchar_t* suffix) {
    const size_t dot = resolvedBase.find_last_of(L'.');
    const size_t sep = resolvedBase.find_last_of(L"/\\");
    if (dot == std::wstring::npos || (sep != std::wstring::npos && dot < sep)) return {};
    const std::wstring pairedWide = resolvedBase.substr(0, dot) + suffix + resolvedBase.substr(dot);
    return FileExists(pairedWide) ? rs::platform_text::WideToUtf8(pairedWide) : std::string();
}

std::wstring NormalizeSlashes(std::wstring s) {
    for (auto& c : s) if (c == kForeignSeparator) c = kSeparator;
    return s;
}

std::wstring ResolvePath(const std::wstring& baseDir, const std::string& relUtf8) {
    std::wstring rel = NormalizeSlashes(rs::platform_text::Utf8ToWide(relUtf8));
    if (rel.empty()) return {};
    // 絶対パス（ドライブ or UNC \\server\share\...）ならそのまま。
    // ドライブ相対(`C:foo`、rel[1]==':'だがrel[2]がセパレータでない)はここでは
    // 区別せず絶対パス扱いにする(元実装からの既存挙動を維持。呼び出し側はいずれも
    // プラグイン定義ファイルの相対パスかフルパスのどちらかしか渡さない)。
#ifdef _WIN32
    if ((rel.size() >= 2 && rel[1] == L':') || (rel.size() >= 2 && rel[0] == L'\\' && rel[1] == L'\\')) {
        return rel;
    }
#else
    if (rel[0] == kSeparator) return rel;
#endif
    std::wstring combined = baseDir;
    if (!combined.empty() && combined.back() != kSeparator) combined += kSeparator;
    combined += rel;

#ifdef _WIN32
    wchar_t full[MAX_PATH * 2];
    DWORD n = GetFullPathNameW(combined.c_str(), MAX_PATH * 2, full, nullptr);
    if (n > 0 && n < MAX_PATH * 2) return std::wstring(full, n);
    return combined;
#else
    // 存在するかどうかに関係なく、.. や . だけを字面だけで解消する(GetFullPathNameWと同じ扱い)。
    return std::filesystem::absolute(std::filesystem::path(combined)).lexically_normal().wstring();
#endif
}

std::wstring DirOf(const std::wstring& path) {
    size_t p = path.find_last_of(L"\\/");
    return p == std::wstring::npos ? std::wstring() : path.substr(0, p);
}

namespace {

// folder の親フォルダ名(Train/xxx なら "Train")。区切りが無ければ空。
std::wstring ParentFolderName(const std::wstring& folder) {
    std::wstring trimmed = folder;
    while (!trimmed.empty() && (trimmed.back() == L'\\' || trimmed.back() == L'/')) trimmed.pop_back();
    const std::wstring parent = DirOf(trimmed);
    const size_t p = parent.find_last_of(L"\\/");
    return p == std::wstring::npos ? parent : parent.substr(p + 1);
}

// folder 直下の *.txt のファイル名(ディレクトリは除く)。並びはファイル名の昇順。
#ifdef _WIN32
std::vector<std::wstring> ListTxtFileNames(const std::wstring& folder) {
    std::vector<std::wstring> names;
    std::wstring pattern = folder + L"\\*.txt";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return names;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        names.push_back(fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return names;
}
#else
// 列挙順が不定なので、NTFSの並びに合わせて大文字小文字を無視した昇順にそろえる。
std::vector<std::wstring> ListTxtFileNames(const std::wstring& folder) {
    std::vector<std::wstring> names;
    std::error_code error;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(std::filesystem::path(folder), error)) {
        std::error_code entryError;
        if (!entry.is_regular_file(entryError)) continue;
        const std::wstring name = entry.path().filename().wstring();
        if (name.size() >= 4 && CompareNoCase(name.c_str() + name.size() - 4, L".txt") == 0) {
            names.push_back(name);
        }
    }
    std::sort(names.begin(), names.end(), [](const std::wstring& a, const std::wstring& b) {
        return CompareNoCase(a.c_str(), b.c_str()) < 0;
    });
    return names;
}
#endif

// フォルダ内の定義ファイルを、親フォルダ名と同じ種別か・-k-build 版かで4つに分けたもの。
struct DefinitionCandidates {
    std::vector<std::wstring> kindKbuild, kindStandard, otherKbuild, otherStandard;
};

DefinitionCandidates CollectDefinitionCandidates(const std::wstring& folder) {
    // RailSim IIは種別ごとのフォルダ(Train/ 等)の下で、その種別の定義ファイル名だけを開く。
    // 1つのフォルダに別種別の定義も同梱されている場合に備え、親フォルダ名と同じ種別の定義を分けておく。
    const std::wstring kindName = ParentFolderName(folder);
    DefinitionCandidates out;
    for (const std::wstring& name : ListTxtFileNames(folder)) {
        std::wstring full = folder + kSeparator + name;
        // 定義ファイルは末尾が "2.txt" または "2k.txt"（Train2/Rail2/Struct2..）
        auto ends = [&](const wchar_t* suf) {
            std::wstring s = suf;
            return name.size() >= s.size() &&
                   CompareNoCase(name.c_str() + name.size() - s.size(), suf) == 0;
        };
        // 末尾の "2k.txt"/"2.txt" を除いた部分(種別名)が親フォルダ名と一致するか。
        auto isKind = [&](size_t suffixLength) {
            return !kindName.empty() && name.size() - suffixLength == kindName.size() &&
                   CompareNoCaseN(name.c_str(), kindName.c_str(), kindName.size()) == 0;
        };
        if (ends(L"2k.txt")) {
            (isKind(6) ? out.kindKbuild : out.otherKbuild).push_back(full);
        } else if (ends(L"2.txt")) {
            (isKind(5) ? out.kindStandard : out.otherStandard).push_back(full);
        }
    }
    return out;
}

void AppendAll(std::vector<std::wstring>& out, const std::vector<std::wstring>& group) {
    out.insert(out.end(), group.begin(), group.end());
}

} // namespace

std::vector<std::wstring> FindDefinitionFiles(const std::wstring& folder) {
    const DefinitionCandidates candidates = CollectDefinitionCandidates(folder);
    std::vector<std::wstring> out;
    AppendAll(out, candidates.kindKbuild);
    AppendAll(out, candidates.kindStandard);
    AppendAll(out, candidates.otherKbuild);
    AppendAll(out, candidates.otherStandard);
    return out;
}

std::wstring PickDefinitionFile(const std::wstring& folder, bool preferKBuild) {
    const DefinitionCandidates candidates = CollectDefinitionCandidates(folder);
    // 親フォルダと同じ種別の定義があればその中から、無ければ残りから選ぶ。
    // -k-build 版と標準版の優先は preferKBuild で決める。
    const bool hasKind = !candidates.kindKbuild.empty() || !candidates.kindStandard.empty();
    const std::vector<std::wstring>& kbuild = hasKind ? candidates.kindKbuild : candidates.otherKbuild;
    const std::vector<std::wstring>& standard = hasKind ? candidates.kindStandard : candidates.otherStandard;
    const std::vector<std::wstring>& first = preferKBuild ? kbuild : standard;
    const std::vector<std::wstring>& second = preferKBuild ? standard : kbuild;
    if (!first.empty()) return first.front();
    if (!second.empty()) return second.front();
    return {};
}

} // namespace rs::legacy_plugin

#include "UserDataDirectory.h"

#include "../EnvironmentVariable.h"

#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace rs::platform_file {
namespace {

constexpr wchar_t kApplicationDirectoryName[] = L"RailsimPlus";

}  // namespace

#ifdef _WIN32
std::filesystem::path UserDataDirectory() {
    const DWORD requiredSize = ::GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (requiredSize == 0) return std::filesystem::path(kApplicationDirectoryName);

    std::wstring localAppData(requiredSize, L'\0');
    const DWORD copiedSize = ::GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData.data(), requiredSize);
    if (copiedSize == 0 || copiedSize >= requiredSize) return std::filesystem::path(kApplicationDirectoryName);
    localAppData.resize(copiedSize);
    return std::filesystem::path(localAppData) / kApplicationDirectoryName;
}
#else
// XDG_DATA_HOME、無ければ ~/.local/share。どちらも無ければ相対パスにする。
std::filesystem::path UserDataDirectory() {
    const char* dataHome = rs::platform::ReadEnvironmentVariable("XDG_DATA_HOME", false);
    if (dataHome != nullptr && dataHome[0] != '\0') {
        return std::filesystem::path(dataHome) / kApplicationDirectoryName;
    }
    const char* home = rs::platform::ReadEnvironmentVariable("HOME", false);
    if (home == nullptr || home[0] == '\0') return std::filesystem::path(kApplicationDirectoryName);
    return std::filesystem::path(home) / ".local" / "share" / kApplicationDirectoryName;
}
#endif

}  // namespace rs::platform_file

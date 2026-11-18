#include "ExecutableDirectory.h"

#include <system_error>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace rs::platform_file {

#ifdef _WIN32
std::filesystem::path ExecutablePath() {
    std::vector<wchar_t> modulePath(MAX_PATH);
    for (;;) {
        const DWORD copiedSize = ::GetModuleFileNameW(nullptr, modulePath.data(),
                                                       static_cast<DWORD>(modulePath.size()));
        if (copiedSize == 0) return {};
        if (copiedSize < modulePath.size() - 1) return std::filesystem::path(modulePath.data());
        modulePath.resize(modulePath.size() * 2);
    }
}
#else
std::filesystem::path ExecutablePath() {
    std::error_code error;
    const std::filesystem::path executable = std::filesystem::read_symlink("/proc/self/exe", error);
    return error ? std::filesystem::path{} : executable;
}
#endif

std::filesystem::path ExecutableDirectory() {
    const std::filesystem::path executable = ExecutablePath();
    return executable.empty() ? std::filesystem::current_path() : executable.parent_path();
}

}  // namespace rs::platform_file

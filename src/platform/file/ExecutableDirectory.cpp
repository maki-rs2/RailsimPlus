#include "ExecutableDirectory.h"

#include <system_error>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace rs::platform_file {

#ifdef _WIN32
std::filesystem::path ExecutableDirectory() {
    std::vector<wchar_t> modulePath(MAX_PATH);
    for (;;) {
        const DWORD copiedSize = ::GetModuleFileNameW(nullptr, modulePath.data(),
                                                       static_cast<DWORD>(modulePath.size()));
        if (copiedSize == 0) return std::filesystem::current_path();
        if (copiedSize < modulePath.size() - 1) {
            return std::filesystem::path(modulePath.data()).parent_path();
        }
        modulePath.resize(modulePath.size() * 2);
    }
}
#else
std::filesystem::path ExecutableDirectory() {
    std::error_code error;
    const std::filesystem::path executable = std::filesystem::read_symlink("/proc/self/exe", error);
    if (error) return std::filesystem::current_path();
    return executable.parent_path();
}
#endif

}  // namespace rs::platform_file

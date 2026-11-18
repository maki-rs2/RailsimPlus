#include "ProcessLaunch.h"

#include <string>
#include <windows.h>

namespace rs::platform_process {

bool LaunchProcess(const std::filesystem::path& executable, const std::filesystem::path& workingDirectory) {
    std::error_code error;
    if (!executable.is_absolute() || !std::filesystem::is_regular_file(executable, error)) return false;
    // パスに空白があっても1つの引数として読まれるよう、コマンドラインは引用符で囲む。
    std::wstring commandLine = L"\"" + executable.wstring() + L"\"";
    STARTUPINFOW startup{};
    startup.cb = static_cast<DWORD>(sizeof(startup));
    PROCESS_INFORMATION process{};
    const BOOL created = CreateProcessW(executable.c_str(), commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr,
                                        workingDirectory.empty() ? nullptr : workingDirectory.c_str(), &startup,
                                        &process);
    if (!created) return false;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

}  // namespace rs::platform_process

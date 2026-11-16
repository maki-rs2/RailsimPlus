#include "AtomicFileWrite.h"

#include <atomic>
#include <cstdint>
#include <fstream>
#include <string>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>

#include <functional>
#include <thread>
#endif

namespace rs::platform_file {
namespace {

constexpr wchar_t kTemporarySuffix[] = L".tmp";

// 同じ宛先へ複数のスレッド・プロセスが同時に書いても一時ファイル名がぶつからないよう、
// プロセスID・スレッドID・呼び出しごとの連番を名前に入れる。
std::wstring TemporarySuffixForThisCall() {
    static std::atomic<std::uint32_t> sequence{0};
#ifdef _WIN32
    const std::uint64_t processId = ::GetCurrentProcessId();
    const std::uint64_t threadId = ::GetCurrentThreadId();
#else
    const std::uint64_t processId = static_cast<std::uint64_t>(::getpid());
    const std::uint64_t threadId = std::hash<std::thread::id>{}(std::this_thread::get_id());
#endif
    return L"." + std::to_wstring(processId) + L"." + std::to_wstring(threadId) +
           L"." + std::to_wstring(sequence.fetch_add(1)) + kTemporarySuffix;
}

}  // namespace

bool WriteFileAtomically(const std::filesystem::path& path, std::string_view content,
                         std::string& outError) {
    const std::filesystem::path temporaryPath = path.wstring() + TemporarySuffixForThisCall();
    std::error_code removeError;
    {
        std::ofstream stream(temporaryPath, std::ios::binary | std::ios::trunc);
        if (!stream.is_open()) {
            outError = "Could not open the temporary file for writing.";
            return false;
        }
        stream.write(content.data(), static_cast<std::streamsize>(content.size()));
        // closeまで見てから置換する。デストラクタ任せだとフラッシュ失敗を取りこぼし、
        // 壊れた一時ファイルで既存ファイルを置き換えてしまう。
        stream.close();
        if (!stream) {
            outError = "Could not write the temporary file.";
            std::filesystem::remove(temporaryPath, removeError);
            return false;
        }
    }

#ifdef _WIN32
    if (::MoveFileExW(temporaryPath.c_str(), path.c_str(),
                      MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0) {
        outError = "Could not replace the destination file (Win32 error " +
                   std::to_string(::GetLastError()) + ").";
        std::filesystem::remove(temporaryPath, removeError);
        return false;
    }
#else
    // POSIXのrenameは宛先があっても原子的に置き換える。
    std::error_code renameError;
    std::filesystem::rename(temporaryPath, path, renameError);
    if (renameError) {
        outError = "Could not replace the destination file (" + renameError.message() + ").";
        std::filesystem::remove(temporaryPath, removeError);
        return false;
    }
#endif
    return true;
}

}  // namespace rs::platform_file

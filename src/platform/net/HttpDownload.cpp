#include "HttpDownload.h"

#include <cstdlib>
#include <fstream>
#include <system_error>
#include <vector>

#include "HttpGet.h"
#include "HttpsRequest.h"

namespace rs::platform_net {
namespace {

// 受信の各段階の待ち時間の上限(ミリ秒)。大きなファイルでも、止まった通信をこの時間で見切る。
constexpr int kDownloadTimeoutMs = 15000;
constexpr std::size_t kChunkBytes = std::size_t{64} * 1024;

// 応答が宣言した本文のサイズ。ヘッダが無い・読めないときは0(上限の判定は受信量でも行う)。
std::uint64_t DeclaredContentLength(HINTERNET request) {
    wchar_t text[32] = {};
    DWORD size = sizeof(text);
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, text, &size,
                             WINHTTP_NO_HEADER_INDEX)) {
        return 0;
    }
    return std::wcstoull(text, nullptr, 10);
}

HttpDownloadResult StreamToFile(HINTERNET request, std::ofstream& file, const HttpDownloadOptions& options) {
    std::vector<char> buffer(kChunkBytes);
    std::uint64_t total = 0;
    for (;;) {
        if (options.stopToken.stop_requested()) return HttpDownloadResult::Cancelled;
        DWORD received = 0;
        if (!WinHttpReadData(request, buffer.data(), static_cast<DWORD>(buffer.size()), &received)) {
            return HttpDownloadResult::ReadFailed;
        }
        if (received == 0) return HttpDownloadResult::Ok;
        total += received;
        if (total > options.maxBytes) return HttpDownloadResult::TooLarge;
        file.write(buffer.data(), static_cast<std::streamsize>(received));
        if (!file) return HttpDownloadResult::WriteFailed;
        if (options.receivedBytes != nullptr) options.receivedBytes->store(total, std::memory_order_relaxed);
    }
}

} // namespace

HttpDownloadResult HttpDownloadToFile(const std::string& url, const std::filesystem::path& destination,
                                      const HttpDownloadOptions& options) {
    if (!IsHttpsUrl(url)) return HttpDownloadResult::InvalidUrl;
    if (options.stopToken.stop_requested()) return HttpDownloadResult::Cancelled;
    if (options.receivedBytes != nullptr) options.receivedBytes->store(0, std::memory_order_relaxed);
    const std::optional<HttpsResponse> response = SendHttpsGet(url, options.userAgent, kDownloadTimeoutMs, true);
    if (!response) return HttpDownloadResult::ConnectFailed;
    if (response->status != HTTP_STATUS_OK) return HttpDownloadResult::BadStatus;
    if (DeclaredContentLength(response->request.get()) > options.maxBytes) return HttpDownloadResult::TooLarge;

    std::ofstream file(destination, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) return HttpDownloadResult::WriteFailed;
    HttpDownloadResult result = StreamToFile(response->request.get(), file, options);
    file.close();
    if (result == HttpDownloadResult::Ok && !file) result = HttpDownloadResult::WriteFailed;
    if (result != HttpDownloadResult::Ok) {
        std::error_code error;
        std::filesystem::remove(destination, error);
    }
    return result;
}

} // namespace rs::platform_net

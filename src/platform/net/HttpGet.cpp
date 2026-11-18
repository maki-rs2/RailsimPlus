#include "HttpGet.h"

#include <chrono>
#include <windows.h>
#include <winhttp.h>
#include <shellapi.h>
#include <spdlog/spdlog.h>

#include "HttpsRequest.h"
#include "../text/Encoding.h"

namespace rs::platform_net {
namespace {
std::optional<std::string> ReadResponse(HINTERNET request) {
    // リリース情報だけを読み、大きすぎる応答と終わらない受信を打ち切る。
    constexpr std::size_t kMaxResponseBytes = std::size_t{1024} * 1024;
    constexpr std::chrono::seconds kReceiveLimit{15};
    const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + kReceiveLimit;
    std::string body;
    char buffer[8192];
    for (;;) {
        if (std::chrono::steady_clock::now() >= deadline) return std::nullopt;
        DWORD received = 0;
        if (!WinHttpReadData(request, buffer, static_cast<DWORD>(sizeof(buffer)), &received)) return std::nullopt;
        if (received == 0) return body;
        if (body.size() + received > kMaxResponseBytes) return std::nullopt;
        body.append(buffer, received);
    }
}
} // namespace

bool IsHttpsUrl(const std::string& url) {
    return url.starts_with("https://") && url.size() > std::string("https://").size() &&
           url.find_first_of("\r\n\t ") == std::string::npos && url.find('\0') == std::string::npos;
}

std::optional<std::string> HttpGet(const std::string& url, const std::string& userAgent) {
    // バックグラウンド確認が終了処理を長く待たせないための通信上限(ミリ秒)。
    constexpr int kTimeoutMs = 5000;
    const std::optional<HttpsResponse> response = SendHttpsGet(url, userAgent, kTimeoutMs, false);
    if (!response) return std::nullopt;
    if (response->status != HTTP_STATUS_OK) {
        spdlog::warn("[update] HTTP status {}", response->status);
        return std::nullopt;
    }
    return ReadResponse(response->request.get());
}

bool OpenHttpsUrl(const std::string& url) {
    if (!IsHttpsUrl(url)) return false;
    const std::wstring wideUrl = rs::platform_text::Utf8ToWide(url);
    const INT_PTR result = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", wideUrl.c_str(),
                                                                  nullptr, nullptr, SW_SHOWNORMAL));
    // ShellExecuteは32以下をエラー値として返す。
    return result > 32;
}
} // namespace rs::platform_net

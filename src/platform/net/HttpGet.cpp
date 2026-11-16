#include "HttpGet.h"

#include <chrono>
#include <memory>
#include <windows.h>
#include <winhttp.h>
#include <shellapi.h>
#include <spdlog/spdlog.h>

#include "../text/Encoding.h"

namespace rs::platform_net {
namespace {
struct InternetCloser {
    void operator()(void* handle) const { WinHttpCloseHandle(handle); }
};
using InternetHandle = std::unique_ptr<void, InternetCloser>;

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
    if (!IsHttpsUrl(url)) return std::nullopt;
    const std::wstring wideUrl = rs::platform_text::Utf8ToWide(url);
    const std::wstring wideAgent = rs::platform_text::Utf8ToWide(userAgent);
    URL_COMPONENTS parts{};
    parts.dwStructSize = static_cast<DWORD>(sizeof(parts));
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wideUrl.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS) {
        return std::nullopt;
    }
    const InternetHandle session(WinHttpOpen(wideAgent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                           WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    // バックグラウンド確認が終了処理を長く待たせないための通信上限(ミリ秒)。
    constexpr int kTimeoutMs = 5000;
    if (!session || !WinHttpSetTimeouts(session.get(), kTimeoutMs, kTimeoutMs, kTimeoutMs, kTimeoutMs)) {
        return std::nullopt;
    }
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.dwExtraInfoLength != 0) path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    const InternetHandle connection(WinHttpConnect(session.get(), host.c_str(), parts.nPort, 0));
    if (!connection) return std::nullopt;
    const InternetHandle request(WinHttpOpenRequest(connection.get(), L"GET", path.c_str(), nullptr,
                                                   WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                   WINHTTP_FLAG_SECURE));
    // Cookie・認証情報を送らず、別URLへの転送も追わない。
    DWORD disabled = WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION | WINHTTP_DISABLE_REDIRECTS;
    if (!request || !WinHttpSetOption(request.get(), WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled)) ||
        !WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.get(), nullptr)) return std::nullopt;
    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX) ||
        status != HTTP_STATUS_OK) {
        spdlog::warn("[update] HTTP status {}", status);
        return std::nullopt;
    }
    return ReadResponse(request.get());
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

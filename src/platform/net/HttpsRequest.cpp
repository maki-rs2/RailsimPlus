#include "HttpsRequest.h"

#include "HttpGet.h"
#include "../text/Encoding.h"

namespace rs::platform_net {

std::optional<HttpsResponse> SendHttpsGet(const std::string& url, const std::string& userAgent, int timeoutMs,
                                          bool followHttpsRedirects) {
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
    HttpsResponse response;
    response.session.reset(WinHttpOpen(wideAgent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                       WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!response.session ||
        !WinHttpSetTimeouts(response.session.get(), timeoutMs, timeoutMs, timeoutMs, timeoutMs)) {
        return std::nullopt;
    }
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.dwExtraInfoLength != 0) path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    response.connection.reset(WinHttpConnect(response.session.get(), host.c_str(), parts.nPort, 0));
    if (!response.connection) return std::nullopt;
    response.request.reset(WinHttpOpenRequest(response.connection.get(), L"GET", path.c_str(), nullptr,
                                              WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                              WINHTTP_FLAG_SECURE));
    // Cookie・認証情報を送らない。別URLへの転送は、呼び出し側が許したときだけ追う。
    DWORD disabled = WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
    if (!followHttpsRedirects) disabled |= WINHTTP_DISABLE_REDIRECTS;
    if (!response.request ||
        !WinHttpSetOption(response.request.get(), WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled)) ||
        !WinHttpSendRequest(response.request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0,
                            0, 0) ||
        !WinHttpReceiveResponse(response.request.get(), nullptr)) return std::nullopt;
    DWORD statusSize = sizeof(response.status);
    if (!WinHttpQueryHeaders(response.request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &response.status, &statusSize,
                             WINHTTP_NO_HEADER_INDEX)) {
        return std::nullopt;
    }
    return response;
}

} // namespace rs::platform_net

//
// HttpsRequest.h - WinHTTPのHTTPS GETを送って応答のヘッダを受けるまでの共通部。net内部用。
//
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <windows.h>
#include <winhttp.h>

namespace rs::platform_net {

struct InternetCloser {
    void operator()(void* handle) const { WinHttpCloseHandle(handle); }
};
using InternetHandle = std::unique_ptr<void, InternetCloser>;

// セッション・接続・リクエストは、この順の逆で閉じる必要があるため、1つにまとめて持つ。
struct HttpsResponse {
    InternetHandle session;
    InternetHandle connection;
    InternetHandle request;
    DWORD status = 0;
};

// urlへGETを送り、応答のヘッダとステータスコードまで受ける。ステータスの判定は呼び出し側で行う。
// Cookieと認証情報は送らない。followHttpsRedirectsが偽ならリダイレクトを追わず、
// 真ならhttpsからhttpへの転送だけ拒否して追う(WinHTTPの既定の方針)。
// 通信の各段階の待ち時間の上限はtimeoutMs。URLがhttpsでないとき、通信に失敗したときはnulloptを返す。
std::optional<HttpsResponse> SendHttpsGet(const std::string& url, const std::string& userAgent, int timeoutMs,
                                          bool followHttpsRedirects);

} // namespace rs::platform_net

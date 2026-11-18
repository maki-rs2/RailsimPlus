//
// HttpDownload.h - HTTPSのGETで受けた本文をファイルへ流す。Windows専用。
//
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <stop_token>
#include <string>

namespace rs::platform_net {

struct HttpDownloadOptions {
    std::string userAgent;
    // 受け取る本文の上限(バイト)。応答の宣言サイズか、受け取った量がこれを超えたら打ち切る。
    std::uint64_t maxBytes = 0;
    // 受け取ったバイト数の累計を、チャンクごとに書き込む。進捗を見るスレッドが読む。不要ならnullptr。
    std::atomic<std::uint64_t>* receivedBytes = nullptr;
    // チャンクごとに確認する。終了処理が待つのは、読み取り1回分の時間だけになる。
    std::stop_token stopToken;
};

enum class HttpDownloadResult {
    Ok,
    InvalidUrl,      // httpsでない・分解できない
    ConnectFailed,   // 接続・送信・ヘッダの受信に失敗した
    BadStatus,       // 200以外
    TooLarge,        // 上限を超えた
    ReadFailed,      // 本文の受信に失敗した(タイムアウトを含む)
    WriteFailed,     // 保存先に書けない
    Cancelled,       // stopTokenで中止した
};

// urlの本文をdestinationへ書く。リダイレクトは、httpsからhttpsへのものだけ追う。
// Ok以外のときは、書きかけのdestinationを削除する。destinationの親フォルダは作らない。
HttpDownloadResult HttpDownloadToFile(const std::string& url, const std::filesystem::path& destination,
                                      const HttpDownloadOptions& options);

} // namespace rs::platform_net

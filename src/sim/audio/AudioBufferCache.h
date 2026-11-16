// AudioBufferCache.h - 音声ファイルのデコードとOpenALバッファの共有キャッシュ。
#pragma once

#include <AL/al.h>

#include <cstddef>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "../../platform/thread/WorkerThread.h"
#include "WavDecoder.h"

namespace rs::sim {

class AudioBufferCache {
public:
    AudioBufferCache();

    // wavPathのバッファを返す。まだ無ければ別スレッドでのデコードを頼んでfalseを返すので、
    // 描画フレームがファイルの読込で止まらない。呼び出し側は次のフレームで聞き直す。
    // 音源データ自体が原因の失敗は結果をキャッシュして再試行しない。OpenAL側の一時的な失敗は
    // キャッシュせず、次回の呼び出しで再試行する。
    bool GetOrCreate(const std::wstring& wavPath, ALuint& outBuffer);
    // 作成済みの音声の長さ[秒]。未作成・失敗は0。
    double DurationSeconds(const std::wstring& wavPath) const;
    // OpenALのバッファを削除して空にする（デバイスが有効なとき）。
    void Release();
    // OpenAL側が既に失われているとき、削除せずに空にする。
    void Discard();

private:
    struct CachedBuffer {
        ALuint buffer = 0;
        DecodedSound decoded;
    };
    struct DecodeResult {
        std::wstring wavPath;
        bool ok = false;
        DecodedSound decoded;
    };
    static constexpr std::size_t kMaxDecodesPerCollect = 64;

    // デコードが終わった音声のOpenALバッファを作る(OpenALを触るので呼び出し側のスレッドで行う)。
    void CollectFinishedDecodes();

    std::unordered_map<std::wstring, CachedBuffer> cache_;
    std::unordered_set<std::wstring> decoding_; // デコードを頼んで結果を待っているパス
    rs::platform_thread::WorkerThread<std::wstring, DecodeResult> worker_; // 他のメンバを使うので最後に置く
};

} // namespace rs::sim

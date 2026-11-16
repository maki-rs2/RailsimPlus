#include "AudioBufferCache.h"

#include <utility>

#include "AudioBufferFactory.h"

namespace rs::sim {

AudioBufferCache::AudioBufferCache()
    : worker_([](const std::wstring& wavPath, std::stop_token) {
          DecodeResult result;
          result.wavPath = wavPath;
          result.ok = WavDecoder{}.Decode(wavPath, result.decoded);
          return result;
      }) {}

void AudioBufferCache::CollectFinishedDecodes() {
    for (DecodeResult& result : worker_.Drain(kMaxDecodesPerCollect)) {
        decoding_.erase(result.wavPath);
        // Discardの前に頼んだ分と頼み直した分が両方届くことがある。
        if (cache_.contains(result.wavPath)) continue;

        // decode失敗は音源データ自体の問題(WAVが無い/壊れている)であり再試行しても変わらないため、
        // buffer==0のCachedBufferをキャッシュへ登録して以後短絡させる。
        if (!result.ok) {
            cache_.insert({result.wavPath, CachedBuffer{}});
            continue;
        }

        ALuint buffer = 0;
        const BufferCreateOutcome outcome = CreateOpenAlBuffer(result.decoded, buffer);
        if (outcome == BufferCreateOutcome::PermanentFailure) {
            // チャンネル数不正・PCM空も音源データ自体が原因なので同様にキャッシュする。
            cache_.insert({result.wavPath, CachedBuffer{}});
            continue;
        }
        if (outcome == BufferCreateOutcome::RetryableFailure) {
            // OpenAL側(デバイス起因の可能性)の失敗はキャッシュせず、次回に再度試みさせる。
            continue;
        }

        CachedBuffer cached;
        cached.buffer = buffer;
        cached.decoded = std::move(result.decoded);
        cache_.insert({result.wavPath, std::move(cached)});
    }
}

bool AudioBufferCache::GetOrCreate(const std::wstring& wavPath, ALuint& outBuffer) {
    CollectFinishedDecodes();
    const std::unordered_map<std::wstring, CachedBuffer>::const_iterator existing = cache_.find(wavPath);
    if (existing != cache_.end()) {
        outBuffer = existing->second.buffer;
        return outBuffer != 0;
    }
    if (decoding_.insert(wavPath).second) worker_.Post(wavPath, 0);
    return false;
}

double AudioBufferCache::DurationSeconds(const std::wstring& wavPath) const {
    const std::unordered_map<std::wstring, CachedBuffer>::const_iterator it = cache_.find(wavPath);
    if (it == cache_.end() || it->second.decoded.channels == 0 || it->second.decoded.sampleRate == 0) {
        return 0.0;
    }
    return static_cast<double>(it->second.decoded.pcm.size()) /
           static_cast<double>(it->second.decoded.channels) /
           static_cast<double>(it->second.decoded.sampleRate);
}

void AudioBufferCache::Release() {
    for (std::pair<const std::wstring, CachedBuffer>& entry : cache_) {
        if (entry.second.buffer != 0) alDeleteBuffers(1, &entry.second.buffer);
    }
    Discard();
}

void AudioBufferCache::Discard() {
    cache_.clear();
    worker_.ClearPending();
    decoding_.clear();
}

} // namespace rs::sim

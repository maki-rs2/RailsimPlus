#include "AudioBufferFactory.h"

#include <cstdint>

#include <spdlog/spdlog.h>

namespace rs::sim {
namespace {

const char* GetAlErrorText(ALenum error) {
    const ALchar* text = alGetString(error);
    return text != nullptr ? text : "unknown OpenAL error";
}

} // namespace

BufferCreateOutcome CreateOpenAlBuffer(const DecodedSound& decoded, ALuint& outBuffer) {
    ALenum format = 0;
    if (decoded.channels == 1) format = AL_FORMAT_MONO16;
    else if (decoded.channels == 2) format = AL_FORMAT_STEREO16;
    else {
        spdlog::error("[audio] 未対応のチャンネル数です: {}", decoded.channels);
        return BufferCreateOutcome::PermanentFailure;
    }
    if (decoded.pcm.empty() || decoded.sampleRate == 0) {
        spdlog::error("[audio] PCM データが空です");
        return BufferCreateOutcome::PermanentFailure;
    }

    ALuint buffer = 0;
    alGenBuffers(1, &buffer);
    if (alGetError() != AL_NO_ERROR) {
        spdlog::error("[audio] OpenAL バッファの生成に失敗しました");
        return BufferCreateOutcome::RetryableFailure;
    }
    const ALsizei byteCount = static_cast<ALsizei>(decoded.pcm.size() * sizeof(int16_t));
    alBufferData(buffer, format, decoded.pcm.data(), byteCount, static_cast<ALsizei>(decoded.sampleRate));
    const ALenum error = alGetError();
    if (error != AL_NO_ERROR) {
        spdlog::error("[audio] OpenAL バッファへのPCM転送に失敗しました: {}", GetAlErrorText(error));
        alDeleteBuffers(1, &buffer);
        return BufferCreateOutcome::RetryableFailure;
    }

    outBuffer = buffer;
    return BufferCreateOutcome::Success;
}

} // namespace rs::sim

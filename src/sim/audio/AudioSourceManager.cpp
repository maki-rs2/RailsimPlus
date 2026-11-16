#include "AudioSourceManager.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

#include <spdlog/spdlog.h>

namespace rs::sim {
namespace {

float ToOpenAlPitch(float pitch) { return std::pow(2.0f, pitch); }

} // namespace

bool AudioSourceManager::LoadSound(const std::string& id, const std::wstring& wavPath) {
    WavDecoder decoder;
    DecodedSound decoded;
    if (!decoder.Decode(wavPath, decoded)) return false;
    LoadedSound sound;
    sound.decoded = std::move(decoded);
    if (!CreateBuffer(sound)) return false;
    std::unordered_map<std::string, LoadedSound>::iterator existing = sounds_.find(id);
    if (existing != sounds_.end() && existing->second.buffer != 0) alDeleteBuffers(1, &existing->second.buffer);
    sounds_[id] = std::move(sound);
    return true;
}

void AudioSourceManager::PlaySound(const std::string& id, float volume, float pitch, float pan) {
    const std::unordered_map<std::string, LoadedSound>::const_iterator it = sounds_.find(id);
    if (it == sounds_.end()) {
        spdlog::warn("[audio] 未ロードのサウンド id={} を再生しようとしました", id);
        return;
    }
    if (it->second.buffer == 0) return;
    if (activeSources_.size() >= kMaxActiveSources) StopAndDeleteOldestSource();
    ALuint source = 0;
    alGenSources(1, &source);
    if (alGetError() != AL_NO_ERROR) {
        spdlog::error("[audio] OpenAL ソースの生成に失敗しました");
        return;
    }
    const float clampedPan = std::clamp(pan, -1.0f, 1.0f);
    alSourcei(source, AL_BUFFER, static_cast<ALint>(it->second.buffer));
    alSourcef(source, AL_GAIN, volume);
    alSourcef(source, AL_PITCH, ToOpenAlPitch(pitch));
    alSourcei(source, AL_SOURCE_RELATIVE, AL_TRUE);
    alSource3f(source, AL_POSITION, clampedPan, 0.0f, 0.0f);
    alSourcePlay(source);
    if (alGetError() != AL_NO_ERROR) {
        spdlog::error("[audio] サウンド id={} の再生に失敗しました", id);
        alDeleteSources(1, &source);
        return;
    }
    activeSources_.push_back(source);
}

void AudioSourceManager::CollectFinishedSources() {
    std::deque<ALuint>::iterator newEnd = std::remove_if(activeSources_.begin(), activeSources_.end(), [](ALuint source) {
        ALint state = AL_STOPPED;
        alGetSourcei(source, AL_SOURCE_STATE, &state);
        if (state != AL_STOPPED) return false;
        alDeleteSources(1, &source);
        return true;
    });
    activeSources_.erase(newEnd, activeSources_.end());
}

void AudioSourceManager::ReleaseOpenAlResources() {
    for (ALuint source : activeSources_) {
        alSourceStop(source);
        alDeleteSources(1, &source);
    }
    activeSources_.clear();
    for (std::pair<const std::string, LoadedSound>& entry : sounds_) {
        if (entry.second.buffer != 0) {
            alDeleteBuffers(1, &entry.second.buffer);
            entry.second.buffer = 0;
        }
    }
}

void AudioSourceManager::DiscardOpenAlResources() {
    activeSources_.clear();
    for (std::pair<const std::string, LoadedSound>& entry : sounds_) entry.second.buffer = 0;
}

void AudioSourceManager::RecreateBuffers() {
    for (std::pair<const std::string, LoadedSound>& entry : sounds_) {
        if (entry.second.buffer == 0 && !CreateBuffer(entry.second)) {
            spdlog::error("[audio] デバイス再初期化時のサウンドバッファ復元に失敗しました: {}", entry.first);
        }
    }
}

void AudioSourceManager::ClearSounds() { sounds_.clear(); }

bool AudioSourceManager::CreateBuffer(LoadedSound& sound) {
    ALuint buffer = 0;
    if (CreateOpenAlBuffer(sound.decoded, buffer) != BufferCreateOutcome::Success) {
        sound.buffer = 0;
        return false;
    }
    sound.buffer = buffer;
    return true;
}

void AudioSourceManager::StopAndDeleteOldestSource() {
    const ALuint source = activeSources_.front();
    activeSources_.pop_front();
    alSourceStop(source);
    alDeleteSources(1, &source);
}

} // namespace rs::sim

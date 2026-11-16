#pragma once

#include <AL/al.h>

#include <cstddef>
#include <deque>
#include <string>
#include <unordered_map>

#include "AudioBufferFactory.h"
#include "WavDecoder.h"

namespace rs::sim {

class AudioSourceManager {
public:
    bool LoadSound(const std::string& id, const std::wstring& wavPath);
    void PlaySound(const std::string& id, float volume, float pitch, float pan);
    void CollectFinishedSources();
    void ReleaseOpenAlResources();
    void DiscardOpenAlResources();
    void RecreateBuffers();
    void ClearSounds();

private:
    struct LoadedSound {
        ALuint buffer = 0;
        DecodedSound decoded;
    };

    static constexpr std::size_t kMaxActiveSources = kMaxActiveAudioSources;

    bool CreateBuffer(LoadedSound& sound);
    void StopAndDeleteOldestSource();

    std::unordered_map<std::string, LoadedSound> sounds_;
    std::deque<ALuint> activeSources_;
};

} // namespace rs::sim

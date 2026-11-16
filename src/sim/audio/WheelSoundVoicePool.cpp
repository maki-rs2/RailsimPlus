#include "WheelSoundVoicePool.h"

#include <spdlog/spdlog.h>

#include "AlSpace.h"
#include "AudioTuning.h"

namespace rs::sim {
namespace {

bool IsVoiceBusy(ALuint source) {
    ALint state = AL_INITIAL;
    alGetSourcei(source, AL_SOURCE_STATE, &state);
    return state == AL_PLAYING || state == AL_PAUSED;
}

} // namespace

ALuint WheelSoundVoicePool::AcquireVoice(const std::string& railFolder) {
    std::vector<ALuint>& voices = voicesByRail_[railFolder];
    for (ALuint source : voices) {
        if (!IsVoiceBusy(source)) return source;
    }
    const std::size_t maxTotal = static_cast<std::size_t>(AudioTuning::Embedded().wheelSoundMaxTotalVoices);
    if (voices.size() >= kWheelSoundVoicesPerRail || totalVoices_ >= maxTotal) return 0;

    ALuint source = 0;
    alGenSources(1, &source);
    if (alGetError() != AL_NO_ERROR) {
        spdlog::error("[audio] OpenAL source creation failed for wheel sound");
        return 0;
    }
    voices.push_back(source);
    ++totalVoices_;
    return source;
}

void WheelSoundVoicePool::Play(const std::vector<WheelSoundEvent>& events, float userTrainVolume) {
    if (events.empty()) return;

    float alListener[3] = {0.0f, 0.0f, 0.0f};
    alGetListener3f(AL_POSITION, &alListener[0], &alListener[1], &alListener[2]);
    const float cullRadius = AudioTuning::Embedded().placedSoundCullRadiusMeters;

    for (const WheelSoundEvent& event : events) {
        float alPosition[3];
        ToAlSpace(event.worldPos, alPosition);
        const float dx = alPosition[0] - alListener[0];
        const float dy = alPosition[1] - alListener[1];
        const float dz = alPosition[2] - alListener[2];
        if (dx * dx + dy * dy + dz * dz > cullRadius * cullRadius) continue;

        // 初めての音はデコードが終わるまで鳴らさない(一瞬の音なので後から鳴らさない)。
        ALuint buffer = 0;
        if (!bufferCache_.GetOrCreate(event.wavPath, buffer)) continue;
        if (event.startOffsetSeconds >= bufferCache_.DurationSeconds(event.wavPath)) continue;

        const ALuint source = AcquireVoice(event.railFolder);
        if (source == 0) continue;

        alSourceStop(source);
        alSourcei(source, AL_BUFFER, static_cast<ALint>(buffer));
        alSourcei(source, AL_LOOPING, AL_FALSE);
        alSourcef(source, AL_GAIN, userTrainVolume);
        alSourcei(source, AL_SOURCE_RELATIVE, AL_FALSE);
        alSourcef(source, AL_REFERENCE_DISTANCE, AudioTuning::Embedded().placedSoundReferenceDistanceMeters);
        alSourcef(source, AL_MAX_DISTANCE, cullRadius);
        alSourcef(source, AL_ROLLOFF_FACTOR, 1.0f);
        alSource3f(source, AL_POSITION, alPosition[0], alPosition[1], alPosition[2]);
        alSource3f(source, AL_VELOCITY, 0.0f, 0.0f, 0.0f);
        alSourcef(source, AL_SEC_OFFSET, static_cast<float>(event.startOffsetSeconds));
        alSourcePlay(source);
        if (alGetError() != AL_NO_ERROR) {
            spdlog::error("[audio] wheel sound playback failed");
        }
    }
}

void WheelSoundVoicePool::DiscardOpenAlResources() {
    voicesByRail_.clear();
    totalVoices_ = 0;
}

void WheelSoundVoicePool::Shutdown() {
    for (const std::pair<const std::string, std::vector<ALuint>>& entry : voicesByRail_) {
        for (ALuint source : entry.second) {
            alSourceStop(source);
            alDeleteSources(1, &source);
        }
    }
    DiscardOpenAlResources();
}

} // namespace rs::sim

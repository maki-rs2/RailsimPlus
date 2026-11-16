#include "AudioEngine.h"

#include <algorithm>

#include "AudioTuning.h"

namespace rs::sim {

bool AudioEngine::Init() {
    if (!deviceContext_.Init()) return false;
    if (deviceContext_.ConsumeReinitFlag()) {
        sourceManager_.DiscardOpenAlResources();
        DiscardPlacedAndWheelResources();
    }
    sourceManager_.RecreateBuffers();
    return true;
}

void AudioEngine::Shutdown() {
    if (deviceContext_.IsValid()) {
        sourceManager_.ReleaseOpenAlResources();
        // バッファを使っている音源を先に削除してから、共有キャッシュのバッファを削除する。
        placedSoundPool_.Shutdown();
        wheelSoundPool_.Shutdown();
        bufferCache_.Release();
    } else {
        sourceManager_.DiscardOpenAlResources();
        DiscardPlacedAndWheelResources();
    }
    sourceManager_.ClearSounds();
    deviceContext_.Shutdown();
    smoothedListenerVelocity_[0] = 0.0f;
    smoothedListenerVelocity_[1] = 0.0f;
    smoothedListenerVelocity_[2] = 0.0f;
    hasListenerPreviousPos_ = false;
}

bool AudioEngine::Update() {
    const bool wasValid = deviceContext_.IsValid();
    const bool isValid = deviceContext_.Update();
    if (deviceContext_.ConsumeReinitFlag()) {
        sourceManager_.DiscardOpenAlResources();
        DiscardPlacedAndWheelResources();
        sourceManager_.RecreateBuffers();
    } else if (wasValid && !isValid) {
        sourceManager_.DiscardOpenAlResources();
        DiscardPlacedAndWheelResources();
    }
    if (!isValid) return false;

    sourceManager_.CollectFinishedSources();
    return true;
}

bool AudioEngine::LoadSound(const std::string& id, const std::wstring& wavPath) {
    if (!deviceContext_.IsValid()) return false;
    return sourceManager_.LoadSound(id, wavPath);
}

void AudioEngine::PlaySound(const std::string& id, float volume, float pitch, float pan) {
    if (!deviceContext_.IsValid()) return;
    sourceManager_.PlaySound(id, volume * userEffectsVolume_, pitch, pan);
}

void AudioEngine::SyncPlacedSounds(const std::vector<PlacedSoundRequest>& requests,
                                   const float listenerPos[3], const float listenerForward[3],
                                   const float listenerUp[3], double gameElapsedSeconds,
                                   double realDeltaSeconds) {
    if (!deviceContext_.IsValid()) return;

    float listenerVelocity[3] = {0.0f, 0.0f, 0.0f};
    if (hasListenerPreviousPos_ && AudioTuning::Embedded().listenerVelocityEnabled) {
        ComputeListenerVelocity(listenerPreviousPos_, listenerPos, gameElapsedSeconds,
                                AudioTuning::Embedded().listenerTeleportThresholdMeters,
                                listenerVelocity);
    }
    listenerPreviousPos_[0] = listenerPos[0];
    listenerPreviousPos_[1] = listenerPos[1];
    listenerPreviousPos_[2] = listenerPos[2];
    hasListenerPreviousPos_ = true;

    SmoothVelocity(smoothedListenerVelocity_, listenerVelocity, realDeltaSeconds,
                   AudioTuning::Embedded().velocitySmoothingSeconds, smoothedListenerVelocity_);
    placedSoundPool_.Sync(requests, listenerPos, listenerForward, listenerUp,
                          smoothedListenerVelocity_, realDeltaSeconds, userTrainVolume_, userEnvironmentVolume_);
}

void AudioEngine::PlayWheelSounds(const std::vector<WheelSoundEvent>& events) {
    if (!deviceContext_.IsValid()) return;
    wheelSoundPool_.Play(events, userTrainVolume_);
}

void AudioEngine::DiscardPlacedAndWheelResources() {
    placedSoundPool_.DiscardOpenAlResources();
    wheelSoundPool_.DiscardOpenAlResources();
    bufferCache_.Discard();
}

std::vector<std::string> AudioEngine::EnumerateOutputDevices() const {
    return deviceContext_.EnumerateOutputDevices();
}

void AudioEngine::SetOutputDeviceName(const std::string& deviceNameUtf8) {
    deviceContext_.SetTargetDeviceName(deviceNameUtf8);
}

const std::string& AudioEngine::OutputDeviceName() const {
    return deviceContext_.TargetDeviceName();
}

void AudioEngine::SetUserMasterVolume(float volume) {
    deviceContext_.SetUserMasterVolume(volume);
}

void AudioEngine::SetUserTrainVolume(float volume) {
    userTrainVolume_ = std::clamp(volume, 0.0f, 1.0f);
}

void AudioEngine::SetUserEnvironmentVolume(float volume) {
    userEnvironmentVolume_ = std::clamp(volume, 0.0f, 1.0f);
}

void AudioEngine::SetUserEffectsVolume(float volume) {
    userEffectsVolume_ = std::clamp(volume, 0.0f, 1.0f);
}

} // namespace rs::sim

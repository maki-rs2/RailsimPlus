#include "PlacedSoundSourcePool.h"

#include "AlSpace.h"
#include "AudioTuning.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

#include <spdlog/spdlog.h>

namespace rs::sim {

float ApplyUserCategoryVolume(float sourceVolume, bool trainSound, float trainVolume, float environmentVolume) {
    return sourceVolume * std::clamp(trainSound ? trainVolume : environmentVolume, 0.0f, 1.0f);
}

namespace {

float DistanceSquared(const PlacedSoundRequest& request, const float listenerPos[3]) {
    const float dx = request.worldPos[0] - listenerPos[0];
    const float dy = request.worldPos[1] - listenerPos[1];
    const float dz = request.worldPos[2] - listenerPos[2];
    return dx * dx + dy * dy + dz * dz;
}

} // namespace

std::size_t PlacedSoundKeyHash::operator()(const PlacedSoundKey& key) const noexcept {
    const std::size_t first = std::hash<std::size_t>{}(key.slotIndex);
    const std::size_t second = std::hash<std::size_t>{}(key.attachmentIndex);
    const std::size_t combined = first ^ (second + 0x9e3779b9U + (first << 6U) + (first >> 2U));
    const std::size_t third = std::hash<std::uint64_t>{}(key.generation);
    return combined ^ (third + 0x9e3779b9U + (combined << 6U) + (combined >> 2U));
}

std::vector<PlacedSoundRequest> SelectPlacedSoundRequests(
    const std::vector<PlacedSoundRequest>& requests, const float listenerPos[3],
    std::size_t maxCount, float cullRadiusMeters) {
    std::vector<PlacedSoundRequest> selected;
    if (maxCount == 0 || cullRadiusMeters < 0.0f) return selected;

    const float cullRadiusSquared = cullRadiusMeters * cullRadiusMeters;
    for (const PlacedSoundRequest& request : requests) {
        if (DistanceSquared(request, listenerPos) <= cullRadiusSquared) selected.push_back(request);
    }
    std::sort(selected.begin(), selected.end(), [&listenerPos](const PlacedSoundRequest& left,
                                                                const PlacedSoundRequest& right) {
        return DistanceSquared(left, listenerPos) < DistanceSquared(right, listenerPos);
    });
    if (selected.size() > maxCount) selected.resize(maxCount);
    return selected;
}

void ComputeListenerVelocity(const float previousPos[3], const float currentPos[3],
                             double gameElapsedSeconds, float teleportThresholdMeters,
                             float outVelocity[3]) {
    outVelocity[0] = 0.0f;
    outVelocity[1] = 0.0f;
    outVelocity[2] = 0.0f;
    if (gameElapsedSeconds <= 0.0) return;

    const float deltaX = currentPos[0] - previousPos[0];
    const float deltaY = currentPos[1] - previousPos[1];
    const float deltaZ = currentPos[2] - previousPos[2];
    const float distance = std::sqrt(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);
    if (distance > teleportThresholdMeters) return;

    outVelocity[0] = static_cast<float>(static_cast<double>(deltaX) / gameElapsedSeconds);
    outVelocity[1] = static_cast<float>(static_cast<double>(deltaY) / gameElapsedSeconds);
    outVelocity[2] = static_cast<float>(static_cast<double>(deltaZ) / gameElapsedSeconds);
}

void SmoothVelocity(const float previousVelocity[3], const float targetVelocity[3],
                    double realDeltaSeconds, float timeConstantSeconds, float outVelocity[3]) {
    if (timeConstantSeconds <= 0.0f) {
        outVelocity[0] = targetVelocity[0];
        outVelocity[1] = targetVelocity[1];
        outVelocity[2] = targetVelocity[2];
        return;
    }
    if (realDeltaSeconds <= 0.0) {
        outVelocity[0] = previousVelocity[0];
        outVelocity[1] = previousVelocity[1];
        outVelocity[2] = previousVelocity[2];
        return;
    }

    const double alpha = 1.0 - std::exp(-realDeltaSeconds / static_cast<double>(timeConstantSeconds));
    for (std::size_t index = 0; index < 3; ++index) {
        const double previous = static_cast<double>(previousVelocity[index]);
        const double target = static_cast<double>(targetVelocity[index]);
        outVelocity[index] = static_cast<float>(std::lerp(previous, target, alpha));
    }
}

double ComputeLoopStartOffsetSeconds(double elapsedRealSeconds, double durationSeconds) {
    if (elapsedRealSeconds <= 0.0 || durationSeconds <= 0.0) return 0.0;

    const double offsetSeconds = std::fmod(elapsedRealSeconds, durationSeconds);
    return offsetSeconds < 0.0 ? offsetSeconds + durationSeconds : offsetSeconds;
}

void PlacedSoundSourcePool::Sync(const std::vector<PlacedSoundRequest>& requests,
                                 const float listenerPos[3], const float listenerForward[3],
                                 const float listenerUp[3], const float listenerVelocity[3],
                                 double realDeltaSeconds, float trainVolume, float environmentVolume) {
    std::unordered_map<PlacedSoundKey, const PlacedSoundRequest*, PlacedSoundKeyHash> requestsByKey;
    requestsByKey.reserve(requests.size());
    for (const PlacedSoundRequest& request : requests) requestsByKey[request.key] = &request;

    for (std::unordered_map<PlacedSoundKey, PlaybackRecord, PlacedSoundKeyHash>::iterator it = playback_.begin();
         it != playback_.end();) {
        const std::unordered_map<PlacedSoundKey, const PlacedSoundRequest*, PlacedSoundKeyHash>::iterator requestIt =
            requestsByKey.find(it->first);
        if (requestIt != requestsByKey.end() && requestIt->second->wavPath == it->second.wavPath) {
            ++it;
            continue;
        }
        it = playback_.erase(it);
    }

    // 範囲の外で始まった音も、要求が現れた時刻を記録する。近づいたとき、始まりからの経過に合わせて鳴らすため。
    for (const PlacedSoundRequest& request : requests) {
        if (playback_.find(request.key) == playback_.end()) {
            playback_.insert({request.key, PlaybackRecord{request.wavPath, std::chrono::steady_clock::now(), false}});
        }
    }

    const std::vector<PlacedSoundRequest> selected =
        SelectPlacedSoundRequests(requests, listenerPos, kMaxActiveSources,
                                  AudioTuning::Embedded().placedSoundCullRadiusMeters);
    std::unordered_map<PlacedSoundKey, const PlacedSoundRequest*, PlacedSoundKeyHash> selectedByKey;
    selectedByKey.reserve(selected.size());
    for (const PlacedSoundRequest& request : selected) selectedByKey[request.key] = &request;

    for (std::unordered_map<PlacedSoundKey, ActiveSource, PlacedSoundKeyHash>::iterator it = active_.begin();
         it != active_.end();) {
        const std::unordered_map<PlacedSoundKey, const PlacedSoundRequest*, PlacedSoundKeyHash>::iterator selectedIt =
            selectedByKey.find(it->first);
        if (selectedIt != selectedByKey.end() && selectedIt->second->wavPath == it->second.wavPath) {
            ++it;
            continue;
        }
        alSourceStop(it->second.source);
        alDeleteSources(1, &it->second.source);
        it = active_.erase(it);
    }

    for (const PlacedSoundRequest& request : selected) {
        std::unordered_map<PlacedSoundKey, ActiveSource, PlacedSoundKeyHash>::iterator activeIt =
            active_.find(request.key);
        if (activeIt != active_.end()) {
            if (!request.loop && !activeIt->second.createdThisFrame) {
                ALint state = AL_INITIAL;
                alGetSourcei(activeIt->second.source, AL_SOURCE_STATE, &state);
                if (state == AL_STOPPED) {
                    alDeleteSources(1, &activeIt->second.source);
                    active_.erase(activeIt);
                    continue;
                }
            }
            float alPosition[3];
            ToAlSpace(request.worldPos, alPosition);
            alSource3f(activeIt->second.source, AL_POSITION, alPosition[0], alPosition[1], alPosition[2]);
            alSourcef(activeIt->second.source, AL_GAIN,
                      ApplyUserCategoryVolume(request.volumeLinear, request.trainSound, trainVolume, environmentVolume));
            SmoothVelocity(activeIt->second.smoothedVelocity, request.velocity, realDeltaSeconds,
                           AudioTuning::Embedded().velocitySmoothingSeconds,
                           activeIt->second.smoothedVelocity);
            float alVelocity[3];
            ToAlSpace(activeIt->second.smoothedVelocity, alVelocity);
            alSource3f(activeIt->second.source, AL_VELOCITY, alVelocity[0], alVelocity[1], alVelocity[2]);
            continue;
        }

        std::unordered_map<PlacedSoundKey, PlaybackRecord, PlacedSoundKeyHash>::iterator playbackIt =
            playback_.find(request.key);
        if (playbackIt == playback_.end()) {
            playbackIt = playback_.insert({request.key, PlaybackRecord{request.wavPath,
                                                                         std::chrono::steady_clock::now(), false}})
                             .first;
        }
        if (playbackIt->second.oneShotFired) continue;

        // デコード中は次のフレームで聞き直す。単発音はまだ鳴らした扱いにならない。
        ALuint buffer = 0;
        if (!bufferCache_.GetOrCreate(request.wavPath, buffer)) continue;
        const double elapsedRealSeconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - playbackIt->second.startTime).count();
        // 範囲の外で始まった単発音は、近づいたときに終わっていれば鳴らさず、途中なら経過ぶん進めて鳴らす。
        if (!request.loop && elapsedRealSeconds >= bufferCache_.DurationSeconds(request.wavPath)) {
            playbackIt->second.oneShotFired = true;
            continue;
        }
        ALuint source = 0;
        alGenSources(1, &source);
        if (alGetError() != AL_NO_ERROR) {
            spdlog::error("[audio] OpenAL source creation failed for placed sound");
            continue;
        }
        alSourcei(source, AL_BUFFER, static_cast<ALint>(buffer));
        alSourcei(source, AL_LOOPING, request.loop ? AL_TRUE : AL_FALSE);
        alSourcef(source, AL_GAIN,
                  ApplyUserCategoryVolume(request.volumeLinear, request.trainSound, trainVolume, environmentVolume));
        alSourcei(source, AL_SOURCE_RELATIVE, AL_FALSE);
        // 既定(AL_REFERENCE_DISTANCE=1.0)のままだと数m離れただけで急減衰し
        // 実質無音になる。カリング半径(AudioTuning::Embedded().placedSoundCullRadiusMeters)全体で緩やかに減衰する
        // ようreference/max distanceを明示する(実際に聞きながら調整する初期値)。
        alSourcef(source, AL_REFERENCE_DISTANCE, AudioTuning::Embedded().placedSoundReferenceDistanceMeters);
        alSourcef(source, AL_MAX_DISTANCE, AudioTuning::Embedded().placedSoundCullRadiusMeters);
        alSourcef(source, AL_ROLLOFF_FACTOR, 1.0f);
        float alPosition[3];
        ToAlSpace(request.worldPos, alPosition);
        alSource3f(source, AL_POSITION, alPosition[0], alPosition[1], alPosition[2]);
        float smoothedVelocity[3] = {request.velocity[0], request.velocity[1], request.velocity[2]};
        float alVelocity[3];
        ToAlSpace(smoothedVelocity, alVelocity);
        alSource3f(source, AL_VELOCITY, alVelocity[0], alVelocity[1], alVelocity[2]);
        const double offsetSeconds =
            request.loop ? ComputeLoopStartOffsetSeconds(elapsedRealSeconds, bufferCache_.DurationSeconds(request.wavPath))
                         : elapsedRealSeconds;
        if (offsetSeconds > 0.0) alSourcef(source, AL_SEC_OFFSET, static_cast<float>(offsetSeconds));
        alSourcePlay(source);
        if (alGetError() != AL_NO_ERROR) {
            spdlog::error("[audio] placed sound playback failed");
            alDeleteSources(1, &source);
            continue;
        }
        if (!request.loop) playbackIt->second.oneShotFired = true;
        active_.insert({request.key, ActiveSource{source, request.wavPath,
                                                  {smoothedVelocity[0], smoothedVelocity[1], smoothedVelocity[2]}, true}});
    }

    for (std::pair<const PlacedSoundKey, ActiveSource>& entry : active_) {
        entry.second.createdThisFrame = false;
    }

    float alListenerPos[3];
    ToAlSpace(listenerPos, alListenerPos);
    alListener3f(AL_POSITION, alListenerPos[0], alListenerPos[1], alListenerPos[2]);
    float alListenerVelocity[3];
    ToAlSpace(listenerVelocity, alListenerVelocity);
    alListener3f(AL_VELOCITY, alListenerVelocity[0], alListenerVelocity[1], alListenerVelocity[2]);
    float alListenerForward[3];
    ToAlSpace(listenerForward, alListenerForward);
    // upはY成分のみでZを含まないため変換不要。
    const ALfloat orientation[6] = {alListenerForward[0], alListenerForward[1], alListenerForward[2],
                                    listenerUp[0], listenerUp[1], listenerUp[2]};
    alListenerfv(AL_ORIENTATION, orientation);
}

void PlacedSoundSourcePool::DiscardOpenAlResources() {
    active_.clear();
    playback_.clear();
}

void PlacedSoundSourcePool::Shutdown() {
    for (const std::pair<const PlacedSoundKey, ActiveSource>& entry : active_) {
        alSourceStop(entry.second.source);
        alDeleteSources(1, &entry.second.source);
    }
    active_.clear();
    playback_.clear();
}

} // namespace rs::sim

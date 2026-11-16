#pragma once

#include <AL/al.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "AudioBufferCache.h"
#include "AudioBufferFactory.h"

namespace rs::sim {

struct PlacedSoundKey {
    std::size_t slotIndex = 0;
    std::size_t attachmentIndex = 0;
    std::uint64_t generation = 0;

    bool operator==(const PlacedSoundKey& other) const {
        return slotIndex == other.slotIndex && attachmentIndex == other.attachmentIndex &&
               generation == other.generation;
    }
};

struct PlacedSoundKeyHash {
    std::size_t operator()(const PlacedSoundKey& key) const noexcept;
};

struct PlacedSoundRequest {
    PlacedSoundKey key;
    std::wstring wavPath;
    float worldPos[3] = {0.0f, 0.0f, 0.0f};
    float velocity[3] = {0.0f, 0.0f, 0.0f};
    float volumeLinear = 1.0f;
    bool loop = true;
    bool trainSound = false;
};

float ApplyUserCategoryVolume(float sourceVolume, bool trainSound, float trainVolume, float environmentVolume);

std::vector<PlacedSoundRequest> SelectPlacedSoundRequests(
    const std::vector<PlacedSoundRequest>& requests, const float listenerPos[3],
    std::size_t maxCount, float cullRadiusMeters);

// カメラ位置の差分からリスナー速度(ゲーム内m/s)を求める。ゲーム内時刻が進まなかった
// フレームと、移動量が閾値を超えた瞬間移動では速度0を返す。
void ComputeListenerVelocity(const float previousPos[3], const float currentPos[3],
                             double gameElapsedSeconds, float teleportThresholdMeters,
                             float outVelocity[3]);

// 速度ベクトルへ指数平滑を掛ける。timeConstantSecondsは実時間の時定数で、0以下なら
// 平滑化せずtargetをそのまま返す。realDeltaSecondsが0以下のフレームは前回値を保持する。
void SmoothVelocity(const float previousVelocity[3], const float targetVelocity[3],
                    double realDeltaSeconds, float timeConstantSeconds, float outVelocity[3]);

// カリングで一時的にAL Sourceを失ったループ音を、鳴り続けていたはずの位置から
// 再開するための再生開始オフセット[秒]を求める。durationSecondsが0以下なら0を返す。
double ComputeLoopStartOffsetSeconds(double elapsedRealSeconds, double durationSeconds);

class PlacedSoundSourcePool {
public:
    // bufferCacheはAudioEngineが持つ共有キャッシュ。バッファの削除はキャッシュの持ち主が行う。
    explicit PlacedSoundSourcePool(AudioBufferCache& bufferCache) : bufferCache_(bufferCache) {}

    static constexpr std::size_t kMaxActiveSources = kMaxActiveAudioSources;

    // listenerForward/listenerUp: リスナー(カメラ)の向き(正規化済み、ワールド座標)。
    // これを毎フレーム反映しないとAL_ORIENTATIONが初期化時の既定向きのまま固定され、
    // カメラが回転するほど左右パンニングが実際の音源位置とずれる。
    void Sync(const std::vector<PlacedSoundRequest>& requests, const float listenerPos[3],
              const float listenerForward[3], const float listenerUp[3],
              const float listenerVelocity[3], double realDeltaSeconds, float trainVolume,
              float environmentVolume);
    void DiscardOpenAlResources();
    void Shutdown();

private:
    struct ActiveSource {
        ALuint source = 0;
        std::wstring wavPath;
        float smoothedVelocity[3] = {0.0f, 0.0f, 0.0f};
        bool createdThisFrame = false;
    };
    struct PlaybackRecord {
        std::wstring wavPath;
        std::chrono::steady_clock::time_point startTime;
        bool oneShotFired = false;
    };

    AudioBufferCache& bufferCache_;
    std::unordered_map<PlacedSoundKey, ActiveSource, PlacedSoundKeyHash> active_;
    std::unordered_map<PlacedSoundKey, PlaybackRecord, PlacedSoundKeyHash> playback_;
};

} // namespace rs::sim

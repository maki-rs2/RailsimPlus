// WheelSoundVoicePool.h - 車輪音（レールの継目）の単発再生用ボイスプール。
//
// 同じRailフォルダの継目音は、そのRailの専用ボイス（AL Source）を最大10個まで使い回す。
// 全ボイスが再生中のとき、新しい要求は捨てる（鳴っている音は奪わない）。
#pragma once

#include <AL/al.h>

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "AudioBufferCache.h"

namespace rs::sim {

// Railフォルダ1つあたりの同時再生数（RailSim2と同じ10スロット）。
inline constexpr std::size_t kWheelSoundVoicesPerRail = 10;

struct WheelSoundEvent {
    std::string railFolder;         // ボイスの持ち主（Railのフォルダ）
    std::wstring wavPath;           // 継目音のファイル（絶対パス）
    float worldPos[3] = {0.0f, 0.0f, 0.0f}; // 鳴らした瞬間の車軸のワールド位置（再生中は動かさない）
    double startOffsetSeconds = 0.0; // 素材の再生開始位置[秒]（遅れ補正済み）
};

class WheelSoundVoicePool {
public:
    // bufferCacheはAudioEngineが持つ共有キャッシュ。バッファの削除はキャッシュの持ち主が行う。
    explicit WheelSoundVoicePool(AudioBufferCache& bufferCache) : bufferCache_(bufferCache) {}

    // 要求を順に再生する。リスナー（AL_POSITION）から聞こえる範囲の外、素材が読めない、
    // 開始位置が素材の長さ以上、空きボイスが無い要求は黙って捨てる。
    void Play(const std::vector<WheelSoundEvent>& events, float userTrainVolume);
    // OpenAL側が失われたとき、削除せずに管理を空にする。
    void DiscardOpenAlResources();
    // 全ボイスを止めて削除する。
    void Shutdown();

private:
    // 空きボイス（再生していないもの）を返す。無ければ上限まで新しく作る。取れなければ0。
    ALuint AcquireVoice(const std::string& railFolder);

    AudioBufferCache& bufferCache_;
    std::unordered_map<std::string, std::vector<ALuint>> voicesByRail_;
    std::size_t totalVoices_ = 0;
};

} // namespace rs::sim

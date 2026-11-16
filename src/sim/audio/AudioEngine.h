#pragma once

#include <string>
#include <vector>

#include "AudioDeviceContext.h"
#include "AudioSourceManager.h"
#include "AudioBufferCache.h"
#include "PlacedSoundSourcePool.h"
#include "WheelSoundVoicePool.h"

namespace rs::sim {

class AudioEngine {
public:
    AudioEngine() : placedSoundPool_(bufferCache_), wheelSoundPool_(bufferCache_) {}
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    // 既定の再生デバイスで音声エンジンを初期化する。失敗時はfalseを返す。
    bool Init();
    void Shutdown();

    // フレームごとに呼び出す。デバイス切断時は再初期化を試行する。
    bool Update();

    // 音声ファイルをidへ読み込む。同じidは置き換える。
    bool LoadSound(const std::string& id, const std::wstring& wavPath);

    // 読み込み済みidを単発再生する。pitchは[-1, 1]、panは[-1(左), 1(右)]。
    void PlaySound(const std::string& id, float volume = 1.0f, float pitch = 0.0f,
                   float pan = 0.0f);

    // 情景固定/可動配置 SoundEffect を毎フレーム同期する。listenerForward/listenerUpは
    // 正規化済みワールド座標(左右パンニングがカメラの向きに追従するために必須)。
    // gameElapsedSecondsはこのフレームで進んだゲーム内秒数で、リスナー速度の分母に使う。
    // カメラ位置差分と同じ区間を測る値を渡すこと(ずれるとドップラーが暴れる)。
    // realDeltaSecondsは速度平滑化の実時間基準の時定数に使い、TimeScaleを掛けない。
    void SyncPlacedSounds(const std::vector<PlacedSoundRequest>& requests, const float listenerPos[3],
                          const float listenerForward[3], const float listenerUp[3],
                          double gameElapsedSeconds, double realDeltaSeconds);

    // 車輪音（レールの継目）を単発で鳴らす。SyncPlacedSoundsの後に呼ぶこと（その時点の
    // リスナー位置で聞こえる範囲を決める）。
    void PlayWheelSounds(const std::vector<WheelSoundEvent>& events);

    // 現在列挙できる再生デバイス名一覧(UTF-8)を返す。
    std::vector<std::string> EnumerateOutputDevices() const;

    // 出力先デバイス名(UTF-8)を設定する。空文字列=システム既定。
    void SetOutputDeviceName(const std::string& deviceNameUtf8);
    const std::string& OutputDeviceName() const;

    // ユーザー設定の全体音量(0〜1)。AudioTuningのmasterVolumeに掛かる。
    void SetUserMasterVolume(float volume);
    // 列車音・環境音のカテゴリ音量(0〜1)。
    void SetUserTrainVolume(float volume);
    void SetUserEnvironmentVolume(float volume);

private:
    // OpenAL側が失われたとき、音源とバッファの管理を削除せずに空にする。
    void DiscardPlacedAndWheelResources();

    AudioDeviceContext deviceContext_;
    AudioSourceManager sourceManager_;
    AudioBufferCache bufferCache_; // 音源プールより先に作る（プールが参照する）
    PlacedSoundSourcePool placedSoundPool_;
    WheelSoundVoicePool wheelSoundPool_;
    float listenerPreviousPos_[3] = {0.0f, 0.0f, 0.0f};
    float smoothedListenerVelocity_[3] = {0.0f, 0.0f, 0.0f};
    bool hasListenerPreviousPos_ = false;
    float userTrainVolume_ = 1.0f;
    float userEnvironmentVolume_ = 1.0f;
};

} // namespace rs::sim

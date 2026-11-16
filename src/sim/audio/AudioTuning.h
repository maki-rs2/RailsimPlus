// AudioTuning.h - PlacedSoundSourcePoolの調整パラメータ(assets/config/audio_tuning.json)。
#pragma once

namespace rs::sim {

struct AudioTuning {
    float placedSoundReferenceDistanceMeters = 10.0f;
    float placedSoundCullRadiusMeters = 100.0f;
    float listenerTeleportThresholdMeters = 20.0f;
    // AL_DOPPLER_FACTOR。1.0は現実の音速を基準にした物理的な値。
    float dopplerFactor = 1.0f;
    // カメラ操作によるピッチ変動を通常時は避ける。
    bool listenerVelocityEnabled = false;
    // 速度の指数平滑に使う実時間基準の時定数。
    float velocitySmoothingSeconds = 0.08f;
    // リスナー全体のAL_GAIN(線形)。1.0が音源ごとの音量そのまま、0.5で約-6dB。
    float masterVolume = 1.0f;
    // 車輪音（レールの継目）のボイス総数の上限。Railフォルダごとの上限（10）とは別に、全Railの合計に掛かる。
    int wheelSoundMaxTotalVoices = 32;

    // 埋め込みJSONをプロセス内で1回だけ解析し、失敗時は構造体の既定値を返す。
    static const AudioTuning& Embedded();
};

} // namespace rs::sim

#pragma once

#include <AL/alc.h>

#include <chrono>
#include <string>
#include <vector>

namespace rs::sim {

class AudioDeviceContext {
public:
    bool Init();
    void Shutdown();
    bool Update();
    bool IsValid() const;
    // 開いていれば、リスナーの音量を0にして鳴っている音をすぐ止める(音源の削除やデバイスを閉じる処理は待たされることがあるため、先に無音にする)。
    void Mute();
    bool ConsumeReinitFlag();

    // 現在のOpenAL Softが列挙できる再生デバイス名一覧(UTF-8)を返す。
    std::vector<std::string> EnumerateOutputDevices() const;

    // 出力先デバイス名(UTF-8)を設定する。空文字列=システム既定。既にデバイスが
    // 開いていれば即座に切替を試みる(失敗時はShutdownしてUpdate()の再初期化
    // リトライへ委ねる)。
    void SetTargetDeviceName(const std::string& deviceNameUtf8);
    const std::string& TargetDeviceName() const { return targetDeviceName_; }

    // ユーザー設定のマスター音量(0〜1)。AudioTuningのmasterVolumeに掛けてリスナーのAL_GAINにする。
    // デバイスが開いていれば即反映し、再初期化後もInit()で掛け直す。
    void SetUserMasterVolume(float volume);

private:
    static constexpr std::chrono::seconds kResetRetryInterval{1};

    ALCdevice* device_ = nullptr;
    ALCcontext* context_ = nullptr;
    std::chrono::steady_clock::time_point lastResetAttempt_{};
    bool inErrorState_ = false;
    bool hasInitialized_ = false;
    bool reinitialized_ = false;
    // 空 = システム既定(自動追従が働く)。UserSettingsData::audioOutputDeviceNameが正典。
    std::string targetDeviceName_;
    float userMasterVolume_ = 1.0f;

    void ApplyListenerGain() const;
};

} // namespace rs::sim

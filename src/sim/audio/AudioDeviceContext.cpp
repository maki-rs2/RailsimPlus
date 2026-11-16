#include "AudioDeviceContext.h"

#include "AudioTuning.h"

#include <AL/al.h>
#include <AL/alext.h>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <atomic>
#include <cstring>

namespace rs::sim {
namespace {

bool IsDeviceConnected(ALCdevice* device) {
    if (device == nullptr || alcIsExtensionPresent(device, "ALC_EXT_disconnect") == ALC_FALSE) return true;
    ALCint connected = ALC_TRUE;
    alcGetIntegerv(device, ALC_CONNECTED, 1, &connected);
    return connected == ALC_TRUE;
}

// OS側の既定再生デバイス変更を通知するコールバックから立てるフラグ。
// ミキサースレッドから呼ばれる可能性があるため、フラグを立てるだけに留める。
std::atomic<bool> g_defaultPlaybackDeviceChanged{false};

void ALC_APIENTRY OnAlcSystemEvent(ALCenum eventType, ALCenum deviceType, ALCdevice* /*device*/, ALCsizei /*length*/,
                                    const ALCchar* /*message*/, void* /*userParam*/) noexcept {
    if (eventType == ALC_EVENT_TYPE_DEFAULT_DEVICE_CHANGED_SOFT && deviceType == ALC_PLAYBACK_DEVICE_SOFT) {
        g_defaultPlaybackDeviceChanged.store(true, std::memory_order_relaxed);
    }
}

// ALC_SOFT_system_events拡張が使えるなら、既定再生デバイス変更イベントの通知を登録する。
// OpenAL SoftはPlaybackFactory(実バックエンド)をalcOpenDevice等InitConfig()を呼ぶ関数の中で
// 初めて遅延初期化するため、一度もデバイスを開いていない状態ではalcEventIsSupportedSOFTが
// 常に「非対応」を返す。そのため呼び出し側(Init())はデバイスのオープンに成功した後にこれを
// 呼ぶこと。失敗結果はキャッシュせず、registeredがtrueになるまで毎回再試行する
// (ただし警告ログは初回失敗時の1回だけ出す。1秒間隔の再初期化リトライでログが連投されるのを防ぐ)。
void EnsureSystemEventsRegistered() {
    static bool registered = false;
    static bool loggedFailure = false;
    if (registered) return;

    const bool ok = [] {
        if (alcIsExtensionPresent(nullptr, "ALC_SOFT_system_events") == ALC_FALSE) {
            if (!loggedFailure) {
                spdlog::warn(
                    "[audio] ALC_SOFT_system_events拡張が見つからないため、既定デバイス変更を自動追従できません");
            }
            return false;
        }
        LPALCEVENTISSUPPORTEDSOFT alcEventIsSupportedSOFT =
            reinterpret_cast<LPALCEVENTISSUPPORTEDSOFT>(alcGetProcAddress(nullptr, "alcEventIsSupportedSOFT"));
        LPALCEVENTCONTROLSOFT alcEventControlSOFT =
            reinterpret_cast<LPALCEVENTCONTROLSOFT>(alcGetProcAddress(nullptr, "alcEventControlSOFT"));
        LPALCEVENTCALLBACKSOFT alcEventCallbackSOFT =
            reinterpret_cast<LPALCEVENTCALLBACKSOFT>(alcGetProcAddress(nullptr, "alcEventCallbackSOFT"));
        if (alcEventIsSupportedSOFT == nullptr || alcEventControlSOFT == nullptr || alcEventCallbackSOFT == nullptr) {
            if (!loggedFailure) {
                spdlog::warn("[audio] ALC_SOFT_system_events の関数アドレス取得に失敗しました");
            }
            return false;
        }
        if (alcEventIsSupportedSOFT(ALC_EVENT_TYPE_DEFAULT_DEVICE_CHANGED_SOFT, ALC_PLAYBACK_DEVICE_SOFT) !=
            ALC_EVENT_SUPPORTED_SOFT) {
            if (!loggedFailure) {
                spdlog::warn("[audio] 現在の再生バックエンドは既定デバイス変更イベントに対応していません");
            }
            return false;
        }
        alcEventCallbackSOFT(OnAlcSystemEvent, nullptr);
        const ALCenum events[] = {ALC_EVENT_TYPE_DEFAULT_DEVICE_CHANGED_SOFT};
        if (alcEventControlSOFT(1, events, ALC_TRUE) != ALC_TRUE) {
            if (!loggedFailure) {
                spdlog::warn("[audio] 既定デバイス変更イベントの有効化に失敗しました");
            }
            return false;
        }
        spdlog::info("[audio] ALC_SOFT_system_events による既定デバイス変更通知を登録しました");
        return true;
    }();

    if (ok) {
        registered = true;
    } else {
        loggedFailure = true;
    }
}

// ALC_SOFT_reopen_device拡張で、既存のcontext/source/bufferを保持したまま
// 指定デバイス(deviceNameがnullptrならOS既定)へ再接続を試みる。手動選択(UI経由)と
// OS既定デバイス変更イベントの両方がこの1つの関数を通る(経路を分けない)。
bool TryReopenDevice(ALCdevice* device, const ALCchar* deviceName) {
    if (device == nullptr) return false;
    if (alcIsExtensionPresent(device, "ALC_SOFT_reopen_device") == ALC_FALSE) return false;
    LPALCREOPENDEVICESOFT alcReopenDeviceSOFT =
        reinterpret_cast<LPALCREOPENDEVICESOFT>(alcGetProcAddress(device, "alcReopenDeviceSOFT"));
    if (alcReopenDeviceSOFT == nullptr) return false;
    return alcReopenDeviceSOFT(device, deviceName, nullptr) == ALC_TRUE;
}

} // namespace

bool AudioDeviceContext::Init() {
    if (IsValid()) return true;
    const bool wantsSpecificDevice = !targetDeviceName_.empty();
    device_ = alcOpenDevice(wantsSpecificDevice ? targetDeviceName_.c_str() : nullptr);
    if (device_ == nullptr && wantsSpecificDevice) {
        spdlog::warn("[audio] 保存済みの出力デバイス '{}' を開けなかったため、システム既定を使用します",
                     targetDeviceName_);
        device_ = alcOpenDevice(nullptr);
    }
    if (device_ == nullptr) {
        spdlog::error("[audio] OpenAL の再生デバイスを開けませんでした");
        return false;
    }
    context_ = alcCreateContext(device_, nullptr);
    if (context_ == nullptr) {
        spdlog::error("[audio] OpenAL コンテキストの生成に失敗しました");
        alcCloseDevice(device_);
        device_ = nullptr;
        return false;
    }
    if (alcMakeContextCurrent(context_) == ALC_FALSE) {
        spdlog::error("[audio] OpenAL コンテキストをカレントにできませんでした");
        alcDestroyContext(context_);
        context_ = nullptr;
        alcCloseDevice(device_);
        device_ = nullptr;
        return false;
    }
    alListener3f(AL_POSITION, 0.0f, 0.0f, 0.0f);
    alListener3f(AL_VELOCITY, 0.0f, 0.0f, 0.0f);
    const ALfloat orientation[] = {0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f};
    alListenerfv(AL_ORIENTATION, orientation);
    // 既定のAL_INVERSE_DISTANCE_CLAMPEDはAL_MAX_DISTANCEでも音量が0まで下がらず
    // clampされた値のまま残るため、PlacedSoundSourcePoolがカリング半径で音源を
    // 削除した瞬間に不連続な無音化(ブツ切れ)として聞こえる。AL_LINEAR_DISTANCE_CLAMPEDは
    // AL_MAX_DISTANCEちょうどで音量0になるため、カリング半径=AL_MAX_DISTANCEと
    // 揃えれば削除時点で既に無音になっており自然に消える。
    alDistanceModel(AL_LINEAR_DISTANCE_CLAMPED);
    // ドップラーの効き具合はグローバル係数で調整する(音源ごとの指定はできない)。
    alDopplerFactor(AudioTuning::Embedded().dopplerFactor);
    ApplyListenerGain();
    reinitialized_ = hasInitialized_;
    hasInitialized_ = true;
    inErrorState_ = false;
    // alcOpenDevice成功後でないとPlaybackFactoryが未初期化のため、判定が常に失敗する。
    // Init()成功末尾のここで呼ぶこと(先頭で呼ばない)。
    EnsureSystemEventsRegistered();
    return true;
}

void AudioDeviceContext::Shutdown() {
    if (context_ != nullptr) {
        alcMakeContextCurrent(nullptr);
        alcDestroyContext(context_);
        context_ = nullptr;
    }
    if (device_ != nullptr) {
        alcCloseDevice(device_);
        device_ = nullptr;
    }
}

bool AudioDeviceContext::Update() {
    // 手動選択中(targetDeviceName_が非空)はOS側の既定変更通知を無視する(短絡評価で
    // exchangeも呼ばれずフラグは保持される。device_がnullptrのときも同様)。
    if (device_ != nullptr && g_defaultPlaybackDeviceChanged.exchange(false, std::memory_order_relaxed) &&
        targetDeviceName_.empty()) {
        spdlog::info("[audio] 既定の再生デバイス変更を検知しました。出力先を切り替えます");
        if (!TryReopenDevice(device_, nullptr)) {
            spdlog::warn("[audio] 出力先の切り替えに失敗しました。再初期化を試行します");
            inErrorState_ = true;
            Shutdown();
        }
    }
    if (device_ != nullptr && !IsDeviceConnected(device_)) {
        if (!inErrorState_) {
            spdlog::warn("[audio] OpenAL 再生デバイスの切断を検知しました。再初期化を試行します");
            inErrorState_ = true;
        }
        Shutdown();
    }
    if (IsValid()) return true;
    if (!inErrorState_) return false;
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    if (now - lastResetAttempt_ < kResetRetryInterval) return false;
    lastResetAttempt_ = now;
    return Init();
}

bool AudioDeviceContext::IsValid() const { return device_ != nullptr && context_ != nullptr; }

bool AudioDeviceContext::ConsumeReinitFlag() {
    const bool reinitialized = reinitialized_;
    reinitialized_ = false;
    return reinitialized;
}

std::vector<std::string> AudioDeviceContext::EnumerateOutputDevices() const {
    const ALCenum specifier = alcIsExtensionPresent(nullptr, "ALC_ENUMERATE_ALL_EXT") == ALC_TRUE
                                   ? ALC_ALL_DEVICES_SPECIFIER
                                   : ALC_DEVICE_SPECIFIER;
    const ALCchar* list = alcGetString(nullptr, specifier);
    std::vector<std::string> names;
    if (list == nullptr) return names;
    // alcGetStringは二重ヌル終端の文字列リストを返す(各デバイス名がヌル終端、
    // リスト全体の終端は空文字列)。
    for (const ALCchar* cursor = list; *cursor != '\0'; cursor += std::strlen(cursor) + 1) {
        names.emplace_back(cursor);
    }
    return names;
}

void AudioDeviceContext::SetTargetDeviceName(const std::string& deviceNameUtf8) {
    if (targetDeviceName_ == deviceNameUtf8) return;
    targetDeviceName_ = deviceNameUtf8;
    if (device_ == nullptr) return; // 未オープン中は次回Init()がtargetDeviceName_を直接使う
    const ALCchar* name = targetDeviceName_.empty() ? nullptr : targetDeviceName_.c_str();
    if (TryReopenDevice(device_, name)) return; // 成功: context/source/buffer維持、reinitialized_も立てない
    spdlog::warn("[audio] 出力デバイスの切替に失敗しました。再初期化を試行します");
    inErrorState_ = true;
    Shutdown();
}

void AudioDeviceContext::SetUserMasterVolume(float volume) {
    userMasterVolume_ = std::clamp(volume, 0.0f, 1.0f);
    if (IsValid()) ApplyListenerGain();
}

void AudioDeviceContext::ApplyListenerGain() const {
    alListenerf(AL_GAIN, AudioTuning::Embedded().masterVolume * userMasterVolume_);
}

} // namespace rs::sim

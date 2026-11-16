//
// UiSoundPlayer.h - 操作に対する効果音(クリック・トグル・タブ・決定・配置・削除・警告・エラー・撮影)を鳴らす
//
// 各所は音の種類(UiSound)だけをキューへ積み、音声エンジンへ渡すのは毎フレーム1か所(Flush)に集める。
// 音の種類と音声ファイルの対応は表(kUiSoundFiles)だけが持つ。同じidの音(TrainRemoveとDelete)は、読込も再生も1回だけにする。
//
#pragma once

#include <array>
#include <vector>

#include "../workspace/EnumIndex.h"

namespace rs::sim {
class AudioEngine;
}

namespace rs::app {

enum class UiSound {
    Click,
    Toggle,
    TabSwitch,
    Confirm,
    NodePlace,
    Delete,
    TrainPlace,
    TrainRemove,
    Warning,
    Error,
    Shutter,
    Focus,
    Count
};
inline constexpr std::size_t kUiSoundCount = static_cast<std::size_t>(UiSound::Count);

class UiSoundPlayer {
public:
    // 音声ファイルをエンジンへ読み込む(エンジンの初期化のあとに呼ぶ)。読めない音は鳴らさない。
    void Load(rs::sim::AudioEngine& engine);
    void Push(UiSound sound) { pending_[workspace::EnumIndex(sound)] = true; }
    // 積んだ音を鳴らす。同じ種類の音(同じidを共有する音を含む)は1フレームに1回だけ。
    void Flush(rs::sim::AudioEngine& engine);

private:
    std::array<bool, kUiSoundCount> pending_{};
    std::array<bool, kUiSoundCount> loaded_{};
};

} // namespace rs::app

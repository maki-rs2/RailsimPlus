#include "GameSettingsPresentation.h"

namespace rs::app::workspace {
GameSettingsPresentation MakeGameSettingsPlaceholder() {
    // 全画面表示の実設定には接続しない仮の値。
    GameSettingsPresentation presentation;
    presentation.fullscreen_ = "off";
    presentation.effectsVolumePercent_ = 100;
    return presentation;
}
} // namespace rs::app::workspace

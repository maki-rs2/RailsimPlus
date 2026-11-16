#pragma once

#include "GameSettingsPresentation.h"

namespace rs::app::workspace {
struct RenderSettingsPresentation {
    // 選択中のプリセットのキー(low・medium・high、どれとも一致しなければcustom)。
    std::string preset_ = "custom";
    std::string label_;
    std::string customLabel_;
    // 画質プリセットの下に出す補足文(空なら何も出さない)。
    std::string note_;
    std::vector<GameSettingsChoice> choices_;
};
} // namespace rs::app::workspace

#pragma once

#include "../action/SignalBlockAction.h"
#include "ImGuiPathPicker.h"

namespace rs::app {

struct SignalBlockContext {
    SignalBlockAction& action;
    ImGuiPathPicker& pathPicker;
};

void DrawSignalBlockPanel(const SignalBlockContext& ctx);

} // namespace rs::app

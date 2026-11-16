#pragma once

namespace rs::app {
struct AppRuntimeState;
void StartUpdateCheck(AppRuntimeState& state);
void PollUpdateCheck(AppRuntimeState& state);
} // namespace rs::app

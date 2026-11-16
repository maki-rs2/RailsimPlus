#pragma once

#include "SceneSlotPool.h"

namespace rs::app {

void UpdateTrainWarpFade(SceneSlotPool::Slot& slot, float fadeOutAlpha,
                           bool spansScenes, double dsMeters, bool groupMoving);
void ResetTrainWarpFade(SceneSlotPool::Slot& slot);

} // namespace rs::app

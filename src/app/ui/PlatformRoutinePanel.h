#pragma once

#include <vector>

#include "../action/PlatformRoutineAction.h"

namespace rs::app {

struct PlatformRoutinePanelState {
    rs::world::TrainId trainIdToAdd = rs::world::NilGuid();
    int tagIdToAdd = -1;
};

struct PlatformRoutineContext {
    PlatformRoutineAction& action;
    PlatformRoutinePanelState& state;
    const std::vector<rs::world::TrainId>& placedTrainIds;
};

void DrawPlatformRoutinePanel(const PlatformRoutineContext& ctx);

}  // namespace rs::app

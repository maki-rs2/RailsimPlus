#pragma once

#include "../action/StationDiaAction.h"
#include "PlatformRoutinePanel.h"
#include "FormationPanel.h"

namespace rs::app {

class SceneRenderer;

struct StationDiaPanelState {
    rs::world::Guid stationBufferId = rs::world::NilGuid();
    rs::world::Guid platformBufferId = rs::world::NilGuid();
    char stationName[256] = {};
    char platformName[256] = {};
    PlatformRoutinePanelState platformRoutine;
};

struct StationDiaContext {
    StationDiaAction& action;
    StationDiaPanelState& state;
    // 対象番線が決まった後にPlatformRoutineActionを構築するためだけに参照する。
    rs::editor::Editor& editor;
    const SceneRenderer* sceneRenderer;
    const TrainPlacementUiState& trainPlacementUi;
};

void DrawStationDiaPanel(const StationDiaContext& ctx);

} // namespace rs::app

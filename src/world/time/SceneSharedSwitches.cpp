//
// SceneSharedSwitches.cpp
//
#include "SceneSharedSwitches.h"

namespace rs::world {

SceneSharedSwitchValues ComputeSceneSharedSwitchValues(const GameClock& clock,
                                                       const SolarState& sun,
                                                       double latitudeDeg,
                                                       double nightThresholdSin,
                                                       bool shadowEnabled,
                                                       bool envMapEnabled) {
    SceneSharedSwitchValues values;
    values.night = sun.directionToSunY < nightThresholdSin ? 1 : 0;
    const int month = clock.Month();
    int season = ((month + 9) % 12) / 3; // 3-5月→0, 6-8月→1, 9-11月→2, 12-2月→3
    if (latitudeDeg < 0.0) season = (season + 2) % 4;
    values.season = season;
    values.shadow = shadowEnabled ? 1 : 0;
    values.envMap = envMapEnabled ? 1 : 0;
    return values;
}

} // namespace rs::world

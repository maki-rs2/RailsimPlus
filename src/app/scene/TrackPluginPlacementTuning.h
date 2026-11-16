#pragma once

namespace rs::app {

struct TrackPluginPlacementTuning {
    double walkStepM = 1.0;
    double yev = 3.0;
    double deflectionScale = 0.3;
    double defaultMaxIntervalM = 40.0;
    double defaultMaxDeflectionM = 0.5;
    double defaultPierIntervalM = 20.0;

    static const TrackPluginPlacementTuning& Embedded();
};

} // namespace rs::app

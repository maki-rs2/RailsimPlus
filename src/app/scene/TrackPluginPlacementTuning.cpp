#include "TrackPluginPlacementTuning.h"

#include "../../core/ConfigJson.h"

#include <nlohmann/json.hpp>

#include "EmbeddedTrackPluginPlacement.h"

namespace rs::app {
namespace {

TrackPluginPlacementTuning ParseEmbedded() {
    return rs::core::ReadEmbeddedConfig<TrackPluginPlacementTuning>(
        rs::embedded::kTrackPluginPlacementJson, rs::embedded::kTrackPluginPlacementJsonSize, "TrackPluginPlacementTuning",
        [](const nlohmann::json& root) {
            TrackPluginPlacementTuning tuning;
            tuning.walkStepM = root.value("walkStepM", tuning.walkStepM);
            tuning.yev = root.value("yev", tuning.yev);
            tuning.deflectionScale = root.value("deflectionScale", tuning.deflectionScale);
            tuning.defaultMaxIntervalM = root.value("defaultMaxIntervalM", tuning.defaultMaxIntervalM);
            tuning.defaultMaxDeflectionM = root.value("defaultMaxDeflectionM", tuning.defaultMaxDeflectionM);
            tuning.defaultPierIntervalM = root.value("defaultPierIntervalM", tuning.defaultPierIntervalM);
            return tuning;
        });
}

} // namespace

const TrackPluginPlacementTuning& TrackPluginPlacementTuning::Embedded() {
    static const TrackPluginPlacementTuning kTuning = ParseEmbedded();
    return kTuning;
}

} // namespace rs::app

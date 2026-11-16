#include "PlacementHeightTuning.h"

#include "../../core/ConfigJson.h"

#include <nlohmann/json.hpp>

#include "EmbeddedPlacementHeightTuning.h"

namespace rs::app {

namespace {

PlacementHeightTuning ParseEmbedded() {
    return rs::core::ReadEmbeddedConfig<PlacementHeightTuning>(
        rs::embedded::kPlacementHeightTuningJson, rs::embedded::kPlacementHeightTuningJsonSize, "PlacementHeightTuning",
        [](const nlohmann::json& root) {
            PlacementHeightTuning tuning;
            tuning.repeatDelaySeconds = root.value("repeatDelaySeconds", tuning.repeatDelaySeconds);
            tuning.repeatStepsPerSecond = root.value("repeatStepsPerSecond", tuning.repeatStepsPerSecond);
            return tuning;
        });
}

} // namespace

const PlacementHeightTuning& PlacementHeightTuning::Embedded() {
    static const PlacementHeightTuning kTuning = ParseEmbedded();
    return kTuning;
}

} // namespace rs::app

#include "TrainInfoCardTuning.h"

#include "../../core/ConfigJson.h"

#include <nlohmann/json.hpp>

#include "EmbeddedTrainInfoCardTuning.h"

namespace rs::app::workspace {

namespace {

TrainInfoCardTuning ParseEmbedded() {
    return rs::core::ReadEmbeddedConfig<TrainInfoCardTuning>(
        rs::embedded::kTrainInfoCardTuningJson, rs::embedded::kTrainInfoCardTuningJsonSize, "TrainInfoCardTuning",
        [](const nlohmann::json& root) {
            TrainInfoCardTuning tuning;
            tuning.maxDistanceM = root.value("maxDistanceM", tuning.maxDistanceM);
            return tuning;
        });
}

} // namespace

const TrainInfoCardTuning& TrainInfoCardTuning::Embedded() {
    static const TrainInfoCardTuning kTuning = ParseEmbedded();
    return kTuning;
}

} // namespace rs::app::workspace

#include "TimeControlTuning.h"

#include "../../core/ConfigJson.h"

#include <nlohmann/json.hpp>
#include "EmbeddedTimeControlTuning.h"

namespace rs::app::workspace {
const TimeControlTuning& TimeControlTuning::Embedded() {
    static const TimeControlTuning kTuning = rs::core::ReadEmbeddedConfig<TimeControlTuning>(
        rs::embedded::kTimeControlTuningJson, rs::embedded::kTimeControlTuningJsonSize, "TimeControlTuning",
        [](const nlohmann::json& root) {
            return TimeControlTuning{root.at("scales").get<std::array<double, 3>>()};
        });
    return kTuning;
}
}

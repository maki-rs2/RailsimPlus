#include "RailLabelHoverTuning.h"

#include "../../core/ConfigJson.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "EmbeddedRailLabelHoverTuning.h"

namespace rs::app::workspace {
namespace {

RailLabelHoverTuning ParseEmbedded() {
    return rs::core::ReadEmbeddedConfig<RailLabelHoverTuning>(
        rs::embedded::kRailLabelHoverTuningJson, rs::embedded::kRailLabelHoverTuningJsonSize,
        "RailLabelHoverTuning", [](const nlohmann::json& root) {
            RailLabelHoverTuning tuning;
            tuning.graceSeconds = root.value("graceSeconds", tuning.graceSeconds);
            return tuning;
        });
}

} // namespace

const RailLabelHoverTuning& RailLabelHoverTuning::Embedded() {
    static const RailLabelHoverTuning kTuning = ParseEmbedded();
    return kTuning;
}

} // namespace rs::app::workspace

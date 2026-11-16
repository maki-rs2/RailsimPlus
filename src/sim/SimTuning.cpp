#include "SimTuning.h"

#include "../core/ConfigJson.h"

#include <nlohmann/json.hpp>

#include "EmbeddedSimTuning.h"  // ビルド時生成(CMakeLists.txt)

namespace rs::sim {

namespace {

SimTuning ParseEmbedded() {
    return rs::core::ReadEmbeddedConfig<SimTuning>(
        rs::embedded::kSimTuningJson, rs::embedded::kSimTuningJsonSize, "SimTuning",
        [](const nlohmann::json& root) {
            SimTuning tuning;
            if (root.contains("approach") && root["approach"].is_object()) {
                const nlohmann::json& approach = root["approach"];
                tuning.approach.warningTimeS =
                    approach.value("warningTimeS", tuning.approach.warningTimeS);
                tuning.approach.minDistanceM =
                    approach.value("minDistanceM", tuning.approach.minDistanceM);
                tuning.approach.stopLimitLineM =
                    approach.value("stopLimitLineM", tuning.approach.stopLimitLineM);
            }
            if (root.contains("signalMargins") && root["signalMargins"].is_object()) {
                const nlohmann::json& signalMargins = root["signalMargins"];
                tuning.signalMargins.movingBlockStopMarginM =
                    signalMargins.value("movingBlockStopMarginM", tuning.signalMargins.movingBlockStopMarginM);
                tuning.signalMargins.pointGuardStopMarginM =
                    signalMargins.value("pointGuardStopMarginM", tuning.signalMargins.pointGuardStopMarginM);
                tuning.signalMargins.entryPermitSpeedKmh =
                    signalMargins.value("entryPermitSpeedKmh", tuning.signalMargins.entryPermitSpeedKmh);
            }
            if (root.contains("deadEndStop") && root["deadEndStop"].is_object()) {
                const nlohmann::json& deadEndStop = root["deadEndStop"];
                tuning.deadEndStop.marginM = deadEndStop.value("marginM", tuning.deadEndStop.marginM);
            }
            if (root.contains("seekAhead") && root["seekAhead"].is_object()) {
                const nlohmann::json& seekAhead = root["seekAhead"];
                tuning.seekAhead.marginM = seekAhead.value("marginM", tuning.seekAhead.marginM);
                tuning.seekAhead.minM = seekAhead.value("minM", tuning.seekAhead.minM);
                tuning.seekAhead.maxM = seekAhead.value("maxM", tuning.seekAhead.maxM);
                tuning.seekAhead.stoppedVelocityEpsilonKmh =
                    seekAhead.value("stoppedVelocityEpsilonKmh", tuning.seekAhead.stoppedVelocityEpsilonKmh);
                tuning.seekAhead.minGroupDecelerationKmhPerS = seekAhead.value(
                    "minGroupDecelerationKmhPerS", tuning.seekAhead.minGroupDecelerationKmhPerS);
            }
            if (root.contains("dia") && root["dia"].is_object()) {
                const nlohmann::json& dia = root["dia"];
                tuning.dia.defaultDwellSeconds = dia.value("defaultDwellSeconds", tuning.dia.defaultDwellSeconds);
            }
            if (root.contains("braking") && root["braking"].is_object()) {
                const nlohmann::json& braking = root["braking"];
                tuning.braking.plannedDecelerationRatio =
                    braking.value("plannedDecelerationRatio", tuning.braking.plannedDecelerationRatio);
                tuning.braking.jerkKmhPerS2 = braking.value("jerkKmhPerS2", tuning.braking.jerkKmhPerS2);
                tuning.braking.maxRampTimeS = braking.value("maxRampTimeS", tuning.braking.maxRampTimeS);
                tuning.braking.releaseDecelerationRatio =
                    braking.value("releaseDecelerationRatio", tuning.braking.releaseDecelerationRatio);
                tuning.braking.substepHz = braking.value("substepHz", tuning.braking.substepHz);
            }
            tuning.occupancyOverlapToleranceM = root.value("occupancyOverlapToleranceM", tuning.occupancyOverlapToleranceM);
            tuning.occupancyNoseToleranceM = root.value("occupancyNoseToleranceM", tuning.occupancyNoseToleranceM);
            return tuning;
        });
}

} // namespace

const SimTuning& SimTuning::Embedded() {
    static const SimTuning kTuning = ParseEmbedded();
    return kTuning;
}

} // namespace rs::sim

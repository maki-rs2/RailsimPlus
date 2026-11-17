#include "CameraTuning.h"

#include "../../core/ConfigJson.h"

#include <nlohmann/json.hpp>

#include "EmbeddedCameraTuning.h"

namespace rs::app {

namespace {

CameraTuning ParseEmbedded() {
    return rs::core::ReadEmbeddedConfig<CameraTuning>(
        rs::embedded::kCameraTuningJson, rs::embedded::kCameraTuningJsonSize, "CameraTuning",
        [](const nlohmann::json& root) {
            CameraTuning tuning;
            tuning.rotationRate = root.value("rotationRate", tuning.rotationRate);
            tuning.zoomRate = root.value("zoomRate", tuning.zoomRate);
            tuning.moveRate = root.value("moveRate", tuning.moveRate);
            tuning.fovRate = root.value("fovRate", tuning.fovRate);
            tuning.flyToMinSeconds = root.value("flyToMinSeconds", tuning.flyToMinSeconds);
            tuning.flyToMaxSeconds = root.value("flyToMaxSeconds", tuning.flyToMaxSeconds);
            tuning.flyToMetersPerSecond = root.value("flyToMetersPerSecond", tuning.flyToMetersPerSecond);
            tuning.rollRadiansPerSecond = root.value("rollRadiansPerSecond", tuning.rollRadiansPerSecond);
            tuning.flyMouseSensitivity = root.value("flyMouseSensitivity", tuning.flyMouseSensitivity);
            tuning.flyPitchLimitRadians = root.value("flyPitchLimitRadians", tuning.flyPitchLimitRadians);
            tuning.viewHeightSensitivity = root.value("viewHeightSensitivity", tuning.viewHeightSensitivity);
            tuning.viewDragPerPixel = root.value("viewDragPerPixel", tuning.viewDragPerPixel);
            tuning.viewDragFastMultiplier = root.value("viewDragFastMultiplier", tuning.viewDragFastMultiplier);
            tuning.viewDistMoveMinMeters = root.value("viewDistMoveMinMeters", tuning.viewDistMoveMinMeters);
            tuning.viewDashMultiplier = root.value("viewDashMultiplier", tuning.viewDashMultiplier);
            tuning.viewZoomStepIn = root.value("viewZoomStepIn", tuning.viewZoomStepIn);
            tuning.viewZoomStepOut = root.value("viewZoomStepOut", tuning.viewZoomStepOut);
            tuning.viewDistanceMinMeters = root.value("viewDistanceMinMeters", tuning.viewDistanceMinMeters);
            tuning.viewDistanceMaxMeters = root.value("viewDistanceMaxMeters", tuning.viewDistanceMaxMeters);
            tuning.fovYWheelStepRadians = root.value("fovYWheelStepRadians", tuning.fovYWheelStepRadians);
            tuning.fovYMinRadians = root.value("fovYMinRadians", tuning.fovYMinRadians);
            tuning.fovYMaxRadians = root.value("fovYMaxRadians", tuning.fovYMaxRadians);
            tuning.cabinMoveMetersPerSecond =
                root.value("cabinMoveMetersPerSecond", tuning.cabinMoveMetersPerSecond);
            tuning.cabinDirMouseSensitivity =
                root.value("cabinDirMouseSensitivity", tuning.cabinDirMouseSensitivity);
            return tuning;
        });
}

} // namespace

const CameraTuning& CameraTuning::Embedded() {
    static const CameraTuning kTuning = ParseEmbedded();
    return kTuning;
}

} // namespace rs::app

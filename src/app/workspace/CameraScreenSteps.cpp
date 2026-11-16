#include "CameraScreenSteps.h"

#include "../../core/ConfigJson.h"

#include <nlohmann/json.hpp>
#include "EmbeddedCameraScreenSteps.h"

namespace rs::app::workspace {
const CameraScreenSteps& CameraScreenSteps::Embedded() {
    static const CameraScreenSteps kSteps = rs::core::ReadEmbeddedConfig<CameraScreenSteps>(
        rs::embedded::kCameraScreenStepsJson, rs::embedded::kCameraScreenStepsJsonSize, "CameraScreenSteps",
        [](const nlohmann::json& root) {
            return CameraScreenSteps{root.at("steps").get<std::array<std::vector<std::string>, kCameraScreenParameterCount>>()};
        });
    return kSteps;
}
}

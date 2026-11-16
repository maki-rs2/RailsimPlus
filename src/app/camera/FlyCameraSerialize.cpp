#include "FlyCameraSerialize.h"

#include <numbers>

#include <spdlog/spdlog.h>

#include "../../world/alignment/JsonReadValue.h"
#include "../../core/Math.h"

namespace rs::app {
namespace {

constexpr const char* kLogTag = "fly_camera_serialize";

}  // namespace

nlohmann::json FlyCameraToJson(const FlyCamera& camera) {
    return {
        { "target", { camera.target[0], camera.target[1], camera.target[2] } },
        { "yaw", camera.yaw * rs::core::kRadToDegF },
        { "pitch", camera.pitch * rs::core::kRadToDegF },
        { "roll", static_cast<double>(camera.roll) * rs::core::kRadToDeg },
        { "distance", camera.distance },
        { "fieldOfView", camera.fieldOfView * rs::core::kRadToDegF },
    };
}

FlyCamera FlyCameraFromJson(const nlohmann::json& json) {
    FlyCamera camera;
    if (!json.is_object()) {
        return camera;
    }

    camera.yaw = rs::world::JsonReadValue(json, "yaw", camera.yaw * rs::core::kRadToDegF, kLogTag) * rs::core::kDegToRadF;
    camera.pitch = rs::world::JsonReadValue(json, "pitch", camera.pitch * rs::core::kRadToDegF, kLogTag) * rs::core::kDegToRadF;
    camera.SetRoll(rs::world::JsonReadValue(json, "roll", 0.0, kLogTag) * rs::core::kDegToRad);
    camera.distance = rs::world::JsonReadValue(json, "distance", camera.distance, kLogTag);
    camera.fieldOfView =
        rs::world::JsonReadValue(json, "fieldOfView", camera.fieldOfView * rs::core::kRadToDegF, kLogTag) * rs::core::kDegToRadF;
    try {
        if (json.contains("target") && json["target"].is_array() &&
            json["target"].size() == 3) {
            camera.target[0] = json["target"][0].get<float>();
            camera.target[1] = json["target"][1].get<float>();
            camera.target[2] = json["target"][2].get<float>();
        }
    } catch (const nlohmann::json::exception&) {
        spdlog::warn("[fly_camera_serialize] 'target' is invalid; using the default value.");
    }
    return camera;
}

}  // namespace rs::app

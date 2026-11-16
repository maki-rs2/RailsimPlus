#include "SceneEntitySerialize.h"

#include "../alignment/GuidJson.h"
#include "../alignment/JsonReadValue.h"

namespace rs::world {
namespace {

constexpr const char* kLogTag = "scene_entity_serialize";

FreePlacement FreePlacementFromEntity(const SceneEntity& entity) {
    const FreePlacement* placement = std::get_if<FreePlacement>(&entity.placement);
    if (placement != nullptr) {
        return *placement;
    }
    spdlog::warn("[scene_entity_serialize] expected FreePlacement for a scenery entity.");
    return {};
}

}  // namespace

nlohmann::json SceneEntityToJson(const SceneEntity& entity) {
    const FreePlacement placement = FreePlacementFromEntity(entity);
    nlohmann::json result = {
        { "guid", GuidToJson(entity.guid) },
        { "plugin", entity.folderUtf8 },
        { "pos", Vec3ToJson(placement.position) },
        { "yawDeg", placement.yawDeg },
        { "pitchDeg", placement.pitchDeg },
        { "rollDeg", placement.rollDeg },
    };
    if (!entity.switchOptions.empty()) {
        result["switchOptions"] = entity.switchOptions;
    }
    if (!entity.sourceAddress.empty()) {
        result["rs2Address"] = entity.sourceAddress;
    }
    return result;
}

SceneEntity SceneEntityFromJson(const nlohmann::json& json) {
    SceneEntity result;
    if (!json.is_object()) {
        return result;
    }
    if (json.contains("guid")) {
        result.guid = GuidFromJson(json["guid"]);
    }
    result.folderUtf8 = JsonReadValue(json, "plugin", result.folderUtf8, kLogTag);
    result.sourceAddress = JsonReadValue(json, "rs2Address", result.sourceAddress, kLogTag);

    FreePlacement placement;
    if (json.contains("pos")) {
        placement.position = Vec3FromJson(json["pos"], placement.position, kLogTag);
    }
    placement.yawDeg = JsonReadValue(json, "yawDeg", placement.yawDeg, kLogTag);
    placement.pitchDeg = JsonReadValue(json, "pitchDeg", placement.pitchDeg, kLogTag);
    placement.rollDeg = JsonReadValue(json, "rollDeg", placement.rollDeg, kLogTag);
    result.placement = placement;

    if (json.contains("switchOptions")) {
        result.switchOptions = JsonReadValue(json, "switchOptions", result.switchOptions, kLogTag);
    }
    return result;
}

}  // namespace rs::world

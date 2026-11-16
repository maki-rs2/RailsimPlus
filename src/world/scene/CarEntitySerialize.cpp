#include "CarEntitySerialize.h"
#include "../alignment/DirectionJson.h"

#include <spdlog/spdlog.h>

#include "../alignment/GuidJson.h"
#include "../alignment/JsonReadValue.h"

namespace rs::world {
namespace {

constexpr const char* kLogTag = "train_entity_serialize";

TrackPlacement TrackPlacementFromEntity(const SceneEntity& entity) {
    const TrackPlacement* placement = std::get_if<TrackPlacement>(&entity.placement);
    if (placement != nullptr) {
        return *placement;
    }
    spdlog::warn("[train_entity_serialize] expected TrackPlacement for a train entity.");
    return {};
}

}  // namespace

nlohmann::json CarEntityToJson(const SceneEntity& entity) {
    const TrackPlacement placement = TrackPlacementFromEntity(entity);
    nlohmann::json result = {
        { "guid", GuidToJson(entity.guid) },
        { "plugin", entity.folderUtf8 },
        { "edge", GuidToJson(placement.position.edge) },
        { "s", placement.position.s },
        { "direction", DirectionToJson(placement.position.direction) },
        { "lateralOffset", placement.lateralOffsetM },
        { "verticalOffset", placement.verticalOffsetM },
        { "keepUpright", placement.keepUpright },
        { "reversed", entity.reversed },
        { "train", GuidIsNil(entity.trainId) ? nlohmann::json(nullptr) : GuidToJson(entity.trainId) },
        { "order", entity.carOrder },
    };
    if (!entity.switchOptions.empty()) {
        result["switchOptions"] = entity.switchOptions;
    }
    return result;
}

SceneEntity CarEntityFromJson(const nlohmann::json& json) {
    SceneEntity result;
    if (!json.is_object()) {
        return result;
    }
    if (json.contains("guid")) {
        result.guid = GuidFromJson(json["guid"]);
    }
    result.folderUtf8 = JsonReadValue(json, "plugin", result.folderUtf8, kLogTag);

    TrackPlacement placement;
    if (json.contains("edge")) {
        placement.position.edge = GuidFromJson(json["edge"]);
    }
    placement.position.s = JsonReadValue(json, "s", placement.position.s, kLogTag);
    placement.position.direction = json.contains("direction") ? DirectionFromJson(json["direction"]) : 1;
    placement.lateralOffsetM = JsonReadValue(json, "lateralOffset", placement.lateralOffsetM, kLogTag);
    placement.verticalOffsetM = JsonReadValue(json, "verticalOffset", placement.verticalOffsetM, kLogTag);
    placement.keepUpright = JsonReadValue(json, "keepUpright", placement.keepUpright, kLogTag);
    result.reversed = JsonReadValue(json, "reversed", result.reversed, kLogTag);
    // 向きの補正は反転フラグから求まるので保存しない。
    placement.yawOffsetDeg = CarYawOffsetDeg(result.reversed);
    result.placement = placement;

    if (json.contains("train") && !json["train"].is_null()) {
        result.trainId = GuidFromJson(json["train"], result.trainId);
    }
    result.carOrder = JsonReadValue(json, "order", result.carOrder, kLogTag);
    if (json.contains("switchOptions")) {
        result.switchOptions = JsonReadValue(json, "switchOptions", result.switchOptions, kLogTag);
    }
    return result;
}

}  // namespace rs::world

#include "ManualPlacementOverlaySerialize.h"
#include "../alignment/DirectionJson.h"

#include <spdlog/spdlog.h>

#include "../alignment/GuidJson.h"
#include "../alignment/JsonReadValue.h"

namespace rs::world {
namespace {

constexpr const char* kLogTag = "manual_placement_overlay_serialize";

template <class T, class ToJson>
nlohmann::json ItemsToJson(const std::vector<T>& items, ToJson toJson) {
    nlohmann::json result = nlohmann::json::array();
    for (const T& item : items) {
        result.push_back(toJson(item));
    }
    return result;
}

template <class T, class FromJson>
std::vector<T> ItemsFromJson(const nlohmann::json& json, FromJson fromJson) {
    std::vector<T> result;
    if (!json.is_array()) {
        return result;
    }
    for (const nlohmann::json& item : json) {
        if (!item.is_object()) {
            continue;
        }
        result.push_back(fromJson(item));
    }
    return result;
}

}  // namespace

nlohmann::json PlacedIndividualToJson(const PlacedIndividual& item) {
    nlohmann::json result = {
        { "guid", GuidToJson(item.id) },
        { "edge", GuidToJson(item.edge) },
        { "s", item.s },
        { "direction", DirectionToJson(item.direction) },
        { "lateralOffset", item.lateralOffsetM },
        { "verticalOffset", item.verticalOffsetM },
        { "yawOffset", item.yawOffsetDeg },
        { "plugin", item.folderUtf8 },
    };
    if (item.hasWorldAnchor) result["worldAnchor"] = { { "pos", {item.worldPos.x, item.worldPos.y, item.worldPos.z} }, { "yaw", item.worldYawDeg } };
    return result;
}

PlacedIndividual PlacedIndividualFromJson(const nlohmann::json& json) {
    PlacedIndividual result;
    if (!json.is_object()) {
        return result;
    }
    if (json.contains("guid")) {
        result.id = GuidFromJson(json["guid"]);
    }
    if (json.contains("edge")) {
        result.edge = GuidFromJson(json["edge"]);
    }
    result.s = JsonReadValue(json, "s", result.s, kLogTag);
    result.direction = json.contains("direction") ? DirectionFromJson(json["direction"]) : 1;
    result.lateralOffsetM = JsonReadValue(json, "lateralOffset", result.lateralOffsetM, kLogTag);
    result.verticalOffsetM = JsonReadValue(json, "verticalOffset", result.verticalOffsetM, kLogTag);
    result.yawOffsetDeg = JsonReadValue(json, "yawOffset", result.yawOffsetDeg, kLogTag);
    result.folderUtf8 = JsonReadValue(json, "plugin", result.folderUtf8, kLogTag);
    result.hasWorldAnchor = json.contains("worldAnchor") && json.at("worldAnchor").is_object();
    if (result.hasWorldAnchor && json.at("worldAnchor").contains("pos") && json.at("worldAnchor").at("pos").is_array() && json.at("worldAnchor").at("pos").size() == 3) {
        try {
            const float worldPosX = json.at("worldAnchor").at("pos")[0].get<float>();
            const float worldPosY = json.at("worldAnchor").at("pos")[1].get<float>();
            const float worldPosZ = json.at("worldAnchor").at("pos")[2].get<float>();
            result.worldPos = rs::core::Vec3(worldPosX, worldPosY, worldPosZ);
        } catch (const nlohmann::json::exception&) {
            spdlog::warn("[manual_placement_overlay_serialize] 'worldPos' is invalid; using the default value.");
        }
    }
    if (result.hasWorldAnchor) result.worldYawDeg = JsonReadValue(json.at("worldAnchor"), "yaw", result.worldYawDeg, kLogTag);
    return result;
}

nlohmann::json WireSpanToJson(const WireSpan& span) {
    return {
        { "guid", GuidToJson(span.id) },
        { "poleA", GuidToJson(span.poleA) },
        { "poleB", GuidToJson(span.poleB) },
        { "branchA", span.branchA },
        { "branchB", span.branchB },
        { "plugin", span.lineFolderUtf8 },
    };
}

WireSpan WireSpanFromJson(const nlohmann::json& json) {
    WireSpan result;
    if (!json.is_object()) {
        return result;
    }
    if (json.contains("guid")) {
        result.id = GuidFromJson(json["guid"]);
    }
    if (json.contains("poleA")) {
        result.poleA = GuidFromJson(json["poleA"]);
    }
    if (json.contains("poleB")) {
        result.poleB = GuidFromJson(json["poleB"]);
    }
    result.branchA = JsonReadValue(json, "branchA", result.branchA, kLogTag);
    result.branchB = JsonReadValue(json, "branchB", result.branchB, kLogTag);
    result.lineFolderUtf8 = JsonReadValue(json, "plugin", result.lineFolderUtf8, kLogTag);
    return result;
}

nlohmann::json RunAnchorToJson(const RunAnchor& section) {
    nlohmann::json result = {
        { "edge", GuidToJson(section.edge) },
        { "plugin", section.folderUtf8 },
    };
    if (section.hasAnchor) {
        result["anchorS"] = section.anchorS;
    }
    return result;
}

RunAnchor RunAnchorFromJson(const nlohmann::json& json) {
    RunAnchor result;
    if (!json.is_object()) {
        return result;
    }
    if (json.contains("edge")) {
        result.edge = GuidFromJson(json["edge"]);
    }
    result.folderUtf8 = JsonReadValue(json, "plugin", result.folderUtf8, kLogTag);
    if (json.contains("anchorS")) {
        result.anchorS = JsonReadValue(json, "anchorS", result.anchorS, kLogTag);
        result.hasAnchor = true;
    }
    return result;
}

nlohmann::json OverlayToJson(const ManualPlacementOverlay& overlay) {
    nlohmann::json result = nlohmann::json::object();
    if (!overlay.poles.empty()) result["poles"] = ItemsToJson(overlay.poles, PlacedIndividualToJson);
    if (!overlay.piers.empty()) result["piers"] = ItemsToJson(overlay.piers, PlacedIndividualToJson);
    if (!overlay.lines.empty()) result["catenaries"] = ItemsToJson(overlay.lines, WireSpanToJson);
    if (!overlay.runAnchors.empty()) result["runAnchors"] = ItemsToJson(overlay.runAnchors, RunAnchorToJson);
    return result;
}

ManualPlacementOverlay OverlayFromJson(const nlohmann::json& json) {
    ManualPlacementOverlay result;
    if (!json.is_object()) {
        return result;
    }
    if (json.contains("poles")) {
        result.poles = ItemsFromJson<PlacedIndividual>(json["poles"], PlacedIndividualFromJson);
    }
    if (json.contains("piers")) {
        result.piers = ItemsFromJson<PlacedIndividual>(json["piers"], PlacedIndividualFromJson);
    }
    if (json.contains("catenaries")) {
        result.lines = ItemsFromJson<WireSpan>(json["catenaries"], WireSpanFromJson);
    }
    if (json.contains("runAnchors")) {
        result.runAnchors = ItemsFromJson<RunAnchor>(
            json["runAnchors"], RunAnchorFromJson);
    }
    return result;
}

}  // namespace rs::world

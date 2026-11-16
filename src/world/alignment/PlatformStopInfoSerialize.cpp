#include "PlatformStopInfoSerialize.h"

#include <spdlog/spdlog.h>

#include "GuidJson.h"
#include "JsonReadValue.h"
#include "PlatformRoutineSerialize.h"

namespace rs::world {
namespace {

constexpr const char* kLogTag = "platform_stop_info_serialize";

const char* StopReferenceToString(StopReference reference) {
    switch (reference) {
    case StopReference::Front:
        return "front";
    case StopReference::Center:
        return "center";
    case StopReference::Rear:
        return "rear";
    }
    return "center";
}

StopReference StopReferenceFromJson(const nlohmann::json& json) {
    if (!json.is_string()) {
        spdlog::warn("[platform_stop_info_serialize] stopReference is invalid; using Center.");
        return StopReference::Center;
    }
    const std::string value = json.get<std::string>();
    if (value == "front") {
        return StopReference::Front;
    }
    if (value == "center") {
        return StopReference::Center;
    }
    if (value == "rear") {
        return StopReference::Rear;
    }
    spdlog::warn("[platform_stop_info_serialize] unknown stopReference '{}'; using Center.", value);
    return StopReference::Center;
}


nlohmann::json TrainRoutinesToJson(const std::vector<PlatformTrainRoutine>& routines) {
    nlohmann::json result = nlohmann::json::array();
    for (const PlatformTrainRoutine& routine : routines) {
        result.push_back({
            { "train", GuidToJson(routine.trainId) },
            { "routine", PlatformRoutineToJson(routine.routine) },
        });
    }
    return result;
}

std::vector<PlatformTrainRoutine> TrainRoutinesFromJson(const nlohmann::json& json) {
    std::vector<PlatformTrainRoutine> result;
    if (!json.is_array()) {
        return result;
    }
    for (const nlohmann::json& item : json) {
        if (!item.is_object()) {
            continue;
        }
        PlatformTrainRoutine routine;
        if (item.contains("train")) {
            routine.trainId = GuidFromJson(item["train"], routine.trainId);
        }
        if (item.contains("routine")) {
            routine.routine = PlatformRoutineFromJson(item["routine"]);
        }
        result.push_back(std::move(routine));
    }
    return result;
}

nlohmann::json FormationRoutinesToJson(const std::vector<PlatformFormationRoutine>& routines) {
    nlohmann::json result = nlohmann::json::array();
    for (const PlatformFormationRoutine& routine : routines) {
        result.push_back({
            { "formation", GuidToJson(routine.formationId) },
            { "routine", PlatformRoutineToJson(routine.routine) },
        });
    }
    return result;
}

std::vector<PlatformFormationRoutine> FormationRoutinesFromJson(const nlohmann::json& json) {
    std::vector<PlatformFormationRoutine> result;
    if (!json.is_array()) {
        return result;
    }
    for (const nlohmann::json& item : json) {
        if (!item.is_object()) {
            continue;
        }
        PlatformFormationRoutine routine;
        if (item.contains("formation")) {
            routine.formationId = GuidFromJson(item["formation"], routine.formationId);
        }
        if (item.contains("routine")) {
            routine.routine = PlatformRoutineFromJson(item["routine"]);
        }
        result.push_back(std::move(routine));
    }
    return result;
}

nlohmann::json TagRoutinesToJson(const std::vector<PlatformTagRoutine>& routines) {
    nlohmann::json result = nlohmann::json::array();
    for (const PlatformTagRoutine& routine : routines) {
        result.push_back({
            { "tagId", routine.tagId },
            { "routine", PlatformRoutineToJson(routine.routine) },
        });
    }
    return result;
}

std::vector<PlatformTagRoutine> TagRoutinesFromJson(const nlohmann::json& json) {
    std::vector<PlatformTagRoutine> result;
    if (!json.is_array()) {
        return result;
    }
    for (const nlohmann::json& item : json) {
        if (!item.is_object()) {
            continue;
        }
        PlatformTagRoutine routine;
        routine.tagId = JsonReadValue(item, "tagId", routine.tagId, kLogTag);
        if (item.contains("routine")) {
            routine.routine = PlatformRoutineFromJson(item["routine"]);
        }
        result.push_back(std::move(routine));
    }
    return result;
}

}  // namespace

nlohmann::json PlatformStopInfoToJson(const PlatformStopInfo& platform) {
    nlohmann::json railEdges = nlohmann::json::array();
    for (const Guid& edge : platform.railEdges) {
        railEdges.push_back(GuidToJson(edge));
    }
    nlohmann::json result = {
        { "guid", GuidToJson(platform.id) },
        { "stationId", GuidToJson(platform.stationId) },
        { "name", platform.name },
        { "stoppable", platform.stoppable },
        { "stopReference", StopReferenceToString(platform.stopReference) },
        { "stopOffset", platform.stopOffsetM },
        { "routine", PlatformRoutineToJson(platform.routine) },
        { "trainRoutines", TrainRoutinesToJson(platform.trainRoutines) },
        { "formationRoutines", FormationRoutinesToJson(platform.formationRoutines) },
        { "tagRoutines", TagRoutinesToJson(platform.tagRoutines) },
        { "rs2Address", platform.stationAddress },
        { "indexInStation", platform.indexInStation },
        { "railEdges", railEdges },
        { "openDoor", { { "left", platform.openDoor[0] }, { "right", platform.openDoor[1] } } },
    };
    if (!GuidIsNil(platform.stationEntity)) result["stationEntity"] = GuidToJson(platform.stationEntity);
    return result;
}

PlatformStopInfo PlatformStopInfoFromJson(const nlohmann::json& json) {
    PlatformStopInfo result;
    if (!json.is_object()) {
        return result;
    }
    if (json.contains("guid")) {
        result.id = GuidFromJson(json["guid"]);
    }
    if (json.contains("stationId")) {
        result.stationId = GuidFromJson(json["stationId"]);
    }
    if (json.contains("stationEntity") && json["stationEntity"].is_string()) {
        result.stationEntity = GuidFromJson(json["stationEntity"]);
    }
    result.name = JsonReadValue(json, "name", result.name, kLogTag);
    result.stoppable = JsonReadValue(json, "stoppable", result.stoppable, kLogTag);
    if (json.contains("stopReference")) {
        result.stopReference = StopReferenceFromJson(json["stopReference"]);
    }
    result.stopOffsetM = JsonReadValue(json, "stopOffset", result.stopOffsetM, kLogTag);
    result.stationAddress = JsonReadValue(json, "rs2Address", result.stationAddress, kLogTag);
    result.indexInStation = JsonReadValue(json, "indexInStation", result.indexInStation, kLogTag);
    if (json.contains("routine")) {
        result.routine = PlatformRoutineFromJson(json["routine"]);
    }
    if (json.contains("trainRoutines")) {
        result.trainRoutines = TrainRoutinesFromJson(json["trainRoutines"]);
    }
    if (json.contains("formationRoutines")) {
        result.formationRoutines = FormationRoutinesFromJson(json["formationRoutines"]);
    }
    if (json.contains("tagRoutines")) {
        result.tagRoutines = TagRoutinesFromJson(json["tagRoutines"]);
    }
    if (json.contains("railEdges") && json["railEdges"].is_array()) {
        for (const nlohmann::json& edge : json["railEdges"]) {
            result.railEdges.push_back(GuidFromJson(edge));
        }
    }
    if (json.contains("openDoor") && json["openDoor"].is_object()) {
        try {
            result.openDoor[0] = json["openDoor"].value("left", result.openDoor[0]);
            result.openDoor[1] = json["openDoor"].value("right", result.openDoor[1]);
        } catch (const nlohmann::json::exception&) {
            spdlog::warn("[platform_stop_info_serialize] openDoor is invalid; using the default value.");
        }
    }
    return result;
}

}  // namespace rs::world

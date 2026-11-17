#include "PlatformRoutineSerialize.h"

#include <spdlog/spdlog.h>

#include "JsonReadValue.h"

namespace rs::world {
namespace {

constexpr const char* kLogTag = "platform_stop_info_serialize";

DiaAction DiaActionFromJson(const nlohmann::json& json) {
    const std::string value = JsonReadValue(json, "action", std::string("stop"), kLogTag);
    if (const std::optional<DiaAction> action = DiaActionFromKey(value)) return *action;
    spdlog::warn("[platform_stop_info_serialize] unknown action '{}'; using Stop.", value);
    return DiaAction::Stop;
}

const char* DiaTimeTypeToString(DiaTimeType timeType) {
    switch (timeType) {
    case DiaTimeType::ElapsedFromArrival:
        return "elapsedFromArrival";
    case DiaTimeType::AbsoluteTimeOfDay:
        return "absoluteTimeOfDay";
    }
    return "elapsedFromArrival";
}

DiaTimeType DiaTimeTypeFromJson(const nlohmann::json& json) {
    const std::string value = JsonReadValue(json, "timeType", std::string("elapsedFromArrival"), kLogTag);
    if (value == "absoluteTimeOfDay") {
        return DiaTimeType::AbsoluteTimeOfDay;
    }
    if (value != "elapsedFromArrival") {
        spdlog::warn("[platform_stop_info_serialize] unknown timeType '{}'; using ElapsedFromArrival.", value);
    }
    return DiaTimeType::ElapsedFromArrival;
}

nlohmann::json DiaTimeOfDayToJson(const DiaTimeOfDay& time) {
    return {
        { "hour", time.hour },
        { "minute", time.minute },
        { "second", time.second },
    };
}

DiaTimeOfDay DiaTimeOfDayFromJson(const nlohmann::json& json) {
    DiaTimeOfDay result;
    if (!json.is_object()) {
        return result;
    }
    result.hour = JsonReadValue(json, "hour", result.hour, kLogTag);
    result.minute = JsonReadValue(json, "minute", result.minute, kLogTag);
    result.second = JsonReadValue(json, "second", result.second, kLogTag);
    return result;
}

nlohmann::json SwitchChangeToJson(const SwitchChange& change) {
    return {
        { "switchName", change.switchName },
        { "value", change.value },
    };
}

SwitchChange SwitchChangeFromJson(const nlohmann::json& json) {
    SwitchChange result;
    if (!json.is_object()) {
        return result;
    }
    result.switchName = JsonReadValue(json, "switchName", result.switchName, kLogTag);
    result.value = JsonReadValue(json, "value", result.value, kLogTag);
    return result;
}

const char* RunInfoChangeFieldToString(RunInfoChange::Field field) {
    switch (field) {
    case RunInfoChange::Field::Type:
        return "type";
    case RunInfoChange::Field::Destination:
        return "destination";
    case RunInfoChange::Field::LineName:
        return "lineName";
    case RunInfoChange::Field::TrainNumber:
        return "trainNumber";
    case RunInfoChange::Field::DutyNumber:
        return "dutyNumber";
    }
    return "type";
}

RunInfoChange::Field RunInfoChangeFieldFromJson(const nlohmann::json& json) {
    const std::string value = JsonReadValue(json, "field", std::string("type"), kLogTag);
    if (value == "destination") {
        return RunInfoChange::Field::Destination;
    }
    if (value == "lineName") {
        return RunInfoChange::Field::LineName;
    }
    if (value == "trainNumber") {
        return RunInfoChange::Field::TrainNumber;
    }
    if (value == "dutyNumber") {
        return RunInfoChange::Field::DutyNumber;
    }
    if (value != "type") {
        spdlog::warn("[platform_stop_info_serialize] unknown runInfoChange field '{}'; using type.", value);
    }
    return RunInfoChange::Field::Type;
}

nlohmann::json RunInfoChangeToJson(const RunInfoChange& change) {
    return {
        { "field", RunInfoChangeFieldToString(change.field) },
        { "value", change.value },
    };
}

RunInfoChange RunInfoChangeFromJson(const nlohmann::json& json) {
    RunInfoChange result;
    if (!json.is_object()) {
        return result;
    }
    result.field = RunInfoChangeFieldFromJson(json);
    result.value = JsonReadValue(json, "value", result.value, kLogTag);
    return result;
}

nlohmann::json PlatformRoutineElementToJson(const PlatformRoutineElement& element) {
    nlohmann::json departureTimes = nlohmann::json::array();
    for (const DiaTimeOfDay& time : element.departureTimes) {
        departureTimes.push_back(DiaTimeOfDayToJson(time));
    }
    nlohmann::json switchChanges = nlohmann::json::array();
    for (const SwitchChange& change : element.switchChanges) {
        switchChanges.push_back(SwitchChangeToJson(change));
    }
    nlohmann::json runInfoChanges = nlohmann::json::array();
    for (const RunInfoChange& change : element.runInfoChanges) {
        runInfoChanges.push_back(RunInfoChangeToJson(change));
    }
    nlohmann::json result = {
        { "enabled", element.enabled },
        { "action", DiaActionKey(element.action) },
        { "timeType", DiaTimeTypeToString(element.timeType) },
        { "dwellSeconds", element.dwellSeconds },
        { "departureTimes", departureTimes },
        { "switchChanges", switchChanges },
        { "runInfoChanges", runInfoChanges },
    };
    if (element.stopOffsetNormalized.has_value()) {
        result["stopOffsetNormalized"] = *element.stopOffsetNormalized;
    }
    return result;
}

PlatformRoutineElement PlatformRoutineElementFromJson(const nlohmann::json& json) {
    PlatformRoutineElement result;
    if (!json.is_object()) {
        return result;
    }
    result.enabled = JsonReadValue(json, "enabled", result.enabled, kLogTag);
    result.action = DiaActionFromJson(json);
    result.timeType = DiaTimeTypeFromJson(json);
    result.dwellSeconds = JsonReadValue(json, "dwellSeconds", result.dwellSeconds, kLogTag);
    if (json.contains("stopOffsetNormalized")) {
        result.stopOffsetNormalized =
            JsonReadValue(json, "stopOffsetNormalized", result.stopOffsetNormalized.value_or(0.0), kLogTag);
    }
    if (json.contains("departureTimes") && json["departureTimes"].is_array()) {
        for (const nlohmann::json& time : json["departureTimes"]) {
            result.departureTimes.push_back(DiaTimeOfDayFromJson(time));
        }
    }
    if (json.contains("switchChanges") && json["switchChanges"].is_array()) {
        for (const nlohmann::json& change : json["switchChanges"]) {
            result.switchChanges.push_back(SwitchChangeFromJson(change));
        }
    }
    if (json.contains("runInfoChanges") && json["runInfoChanges"].is_array()) {
        for (const nlohmann::json& change : json["runInfoChanges"]) {
            result.runInfoChanges.push_back(RunInfoChangeFromJson(change));
        }
    }
    return result;
}

}  // namespace

nlohmann::json PlatformRoutineToJson(const std::vector<PlatformRoutineElement>& routine) {
    nlohmann::json result = nlohmann::json::array();
    for (const PlatformRoutineElement& element : routine) {
        result.push_back(PlatformRoutineElementToJson(element));
    }
    return result;
}

std::vector<PlatformRoutineElement> PlatformRoutineFromJson(const nlohmann::json& json) {
    std::vector<PlatformRoutineElement> result;
    if (!json.is_array()) {
        return result;
    }
    for (const nlohmann::json& element : json) {
        result.push_back(PlatformRoutineElementFromJson(element));
    }
    return result;
}

}  // namespace rs::world

#include "TrainRunInfoRegistrySerialize.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "../alignment/GuidJson.h"

namespace rs::world {
namespace {

nlohmann::json RunInfoToJson(const TrainRunInfo& info) {
    nlohmann::json stopStations = nlohmann::json::array();
    for (const Guid& station : info.stopStations) {
        stopStations.push_back(GuidToJson(station));
    }
    return {
        { "type", info.type },
        { "destination", info.destination },
        { "lineName", info.lineName },
        { "trainNumber", info.trainNumber },
        { "dutyNumber", info.dutyNumber },
        { "lastDepartureStation", GuidToJson(info.lastDepartureStation) },
        { "stopStations", std::move(stopStations) },
    };
}

TrainRunInfo RunInfoFromJson(const nlohmann::json& json) {
    TrainRunInfo result;
    if (!json.is_object()) {
        return result;
    }
    result.type = json.value("type", result.type);
    result.destination = json.value("destination", result.destination);
    result.lineName = json.value("lineName", result.lineName);
    result.trainNumber = json.value("trainNumber", result.trainNumber);
    result.dutyNumber = json.value("dutyNumber", result.dutyNumber);
    if (json.contains("lastDepartureStation")) {
        result.lastDepartureStation = GuidFromJson(json["lastDepartureStation"]);
    }
    if (json.contains("stopStations") && json["stopStations"].is_array()) {
        for (const nlohmann::json& station : json["stopStations"]) {
            result.stopStations.push_back(GuidFromJson(station));
        }
    }
    return result;
}

}  // namespace

nlohmann::json TrainRunInfoRegistryToJson(
    const std::unordered_map<TrainId, TrainRunInfo, GuidHash>& assignments) {
    nlohmann::json result = nlohmann::json::array();
    for (const std::pair<const TrainId, TrainRunInfo>& assignment : assignments) {
        result.push_back({
            { "train", GuidToJson(assignment.first) },
            { "info", RunInfoToJson(assignment.second) },
        });
    }
    return result;
}

TrainRunInfoRegistrySnapshot TrainRunInfoRegistryFromJson(const nlohmann::json& json) {
    TrainRunInfoRegistrySnapshot result;
    if (!json.is_array()) {
        spdlog::warn("[train_run_info_registry_serialize] assignments is not an array; skipping it.");
        return result;
    }
    for (const nlohmann::json& item : json) {
        if (!item.is_object()) {
            spdlog::warn("[train_run_info_registry_serialize] assignment is not an object; skipping it.");
            continue;
        }
        try {
            TrainRunInfoAssignment assignment;
            if (!item.contains("train") || !item.contains("info")) {
                spdlog::warn("[train_run_info_registry_serialize] assignment is incomplete; skipping it.");
                continue;
            }
            assignment.trainId = GuidFromJson(item["train"]);
            assignment.info = RunInfoFromJson(item["info"]);
            result.assignments.push_back(std::move(assignment));
        } catch (const nlohmann::json::exception&) {
            spdlog::warn("[train_run_info_registry_serialize] assignment is invalid; skipping it.");
        }
    }
    return result;
}

}  // namespace rs::world

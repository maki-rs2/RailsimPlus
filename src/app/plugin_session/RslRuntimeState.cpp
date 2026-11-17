#include "RslRuntimeState.h"
#include "../../core/SpeedUnits.h"

#include <map>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "../../world/alignment/GuidJson.h"
#include "../../world/alignment/TrackTraversal.h"

namespace rs::app {
namespace {

const char* RoutineLayerToString(int layerKey) {
    if (layerKey == rs::world::kCommonRoutineLayerKey) return "common";
    return "tag";
}

bool RoutineLayerFromJson(const nlohmann::json& item, int& outLayerKey) {
    const std::string layer = item.value("layer", std::string());
    if (layer == "common") {
        outLayerKey = rs::world::kCommonRoutineLayerKey;
        return true;
    }
    if (layer == "tag" && item.contains("tag")) {
        outLayerKey = rs::world::TagRoutineLayerKey(item.value("tag", -1));
        return true;
    }
    return false;
}

bool DiaActionFromJson(const nlohmann::json& json, rs::world::DiaAction& outAction) {
    if (!json.is_string()) return false;
    const std::string value = json.get<std::string>();
    const std::optional<rs::world::DiaAction> action = rs::world::DiaActionFromKey(value);
    if (!action) spdlog::warn("[rsl_runtime] 未知のdiaアクション '{}' のためStopとして扱います。", value);
    outAction = action.value_or(rs::world::DiaAction::Stop);
    return true;
}

std::string DiaStateToString(rs::sim::DiaState state) {
    switch (state) {
    case rs::sim::DiaState::Running: return "running";
    case rs::sim::DiaState::Braking: return "braking";
    case rs::sim::DiaState::Stopped: return "stopped";
    case rs::sim::DiaState::Departing: return "departing";
    }
    return "running";
}

bool DiaStateFromJson(const nlohmann::json& json, rs::sim::DiaState& outState) {
    const std::string value = json.get<std::string>();
    if (value == "running") outState = rs::sim::DiaState::Running;
    else if (value == "braking") outState = rs::sim::DiaState::Braking;
    else if (value == "stopped") outState = rs::sim::DiaState::Stopped;
    else if (value == "departing") outState = rs::sim::DiaState::Departing;
    else return false;
    return true;
}

nlohmann::json CursorsToJson(const rs::sim::DiaRuntimeSnapshot& snapshot) {
    nlohmann::json cursors = nlohmann::json::array();
    for (const auto& [platformId, layers] : snapshot.routineCursors) {
        for (const auto& [layerKey, cursor] : layers) {
            nlohmann::json cursorJson = { { "platform", rs::world::GuidToJson(platformId) },
                                          { "layer", RoutineLayerToString(layerKey) },
                                          { "cursor", cursor } };
            if (layerKey != rs::world::kCommonRoutineLayerKey) {
                cursorJson["tag"] = -layerKey - 2;
            }
            cursors.push_back(std::move(cursorJson));
        }
    }
    for (const auto& [platformId, groups] : snapshot.trainRoutineCursors) {
        for (const auto& [trainId, cursor] : groups) {
            cursors.push_back({ { "platform", rs::world::GuidToJson(platformId) },
                                { "layer", "train" },
                                { "train", rs::world::GuidToJson(trainId) }, { "cursor", cursor } });
        }
    }
    return cursors;
}

bool CursorsFromJson(const nlohmann::json& json, rs::sim::DiaRuntimeSnapshot& outSnapshot) {
    if (!json.is_array()) return false;
    for (const nlohmann::json& item : json) {
        if (!item.is_object() || !item.contains("platform") || !item.contains("cursor")) return false;
        const rs::world::Guid platformId = rs::world::GuidFromJson(item["platform"]);
        if (rs::world::GuidIsNil(platformId)) return false;
        const int cursor = item.value("cursor", 0);
        const std::string layer = item.value("layer", std::string());
        if (layer == "train" && item.contains("train")) {
            const rs::world::Guid trainId = rs::world::GuidFromJson(item["train"]);
            if (rs::world::GuidIsNil(trainId)) return false;
            outSnapshot.trainRoutineCursors[platformId][trainId] = cursor;
        } else if (layer == "common" || layer == "tag") {
            int layerKey = rs::world::kCommonRoutineLayerKey;
            if (!RoutineLayerFromJson(item, layerKey)) return false;
            outSnapshot.routineCursors[platformId][layerKey] = cursor;
        } else {
            return false;
        }
    }
    return true;
}

}  // namespace

nlohmann::json RslRuntimeStateToJson(
    const RslRuntimeState& state,
    const std::function<const rs::world::TrackGraph*(size_t)>& graphForScene) {
    std::map<rs::world::TrainId, rs::sim::DiaRuntimeGroupSnapshot> diaByGroup;
    for (const rs::sim::DiaRuntimeGroupSnapshot& dia : state.dia.groups) diaByGroup[dia.trainId] = dia;
    nlohmann::json trains = nlohmann::json::array();
    for (const rs::sim::TrainRuntimeSnapshot& train : state.trains) {
        const rs::world::TrackGraph* graph = graphForScene(train.sceneIndex);
        if (graph == nullptr) continue;
        if (!graph->edges.contains(train.kin.head.edge)) continue;
        nlohmann::json trainJson = {
            { "guid", rs::world::GuidToJson(train.trainId) },
            { "speed", train.kin.speed * rs::core::kMetersPerSecondToKilometersPerHour },
            { "targetSpeed", train.kin.targetSpeed * rs::core::kMetersPerSecondToKilometersPerHour },
            { "started", train.kin.started }, { "stopDistance", train.kin.stopDistance },
            { "arrivedAtStop", train.kin.arrivedAtStop },
            { "lastTravelReversed", train.lastTravelReversed },
            { "stopTargetReversed", train.stopTargetReversed },
            { "targetPlatform", rs::world::GuidToJson(train.targetPlatform) },
            { "departedPlatform", rs::world::GuidToJson(train.departedPlatform) },
        };
        const auto diaIterator = diaByGroup.find(train.trainId);
        if (diaIterator != diaByGroup.end()) {
            const rs::sim::DiaRuntimeGroupSnapshot& dia = diaIterator->second;
            trainJson["schedule"] = { { "phase", DiaStateToString(dia.state) },
                               { "phaseElapsed", dia.phaseElapsed },
                               { "visitPlatform", rs::world::GuidToJson(dia.visitPlatform) },
                               { "departedPlatform", rs::world::GuidToJson(dia.departedPlatform) },
                               { "departureScheduled", dia.departureScheduled },
                               { "departureAt", dia.departureAbsSeconds },
                               { "doorCloseAt", dia.doorCloseAbsSeconds },
                               { "action", rs::world::DiaActionKey(dia.currentAction) } };
        }
        trains.push_back(std::move(trainJson));
    }
    return { { "version", 1 }, { "trains", std::move(trains) },
             { "routineCursors", CursorsToJson(state.dia) } };
}

namespace {
// 列車1本分を読む。読めなければ false を返し、出力は使わない。
bool TrainRuntimeFromJson(const nlohmann::json& trainJson, const RslTrainHeads& heads,
                          rs::sim::TrainRuntimeSnapshot& parsedTrain,
                          std::optional<rs::sim::DiaRuntimeGroupSnapshot>& parsedDia) {
    if (!trainJson.is_object() || !trainJson.contains("guid")) return false;
    rs::sim::TrainRuntimeSnapshot train;
    train.trainId = rs::world::GuidFromJson(trainJson["guid"]);
    if (rs::world::GuidIsNil(train.trainId)) return false;
    // 位置と Scene は保存せず、先頭車から求める。先頭車が読み込めなかった列車は飛ばす。
    const auto headIterator = heads.find(train.trainId);
    if (headIterator == heads.end() || !headIterator->second) return false;
    train.sceneIndex = headIterator->second->sceneIndex;
    train.kin.head = headIterator->second->head;
    if (train.kin.head.direction != -1 && train.kin.head.direction != +1) return false;
    train.kin.speed = trainJson.value("speed", 0.0) / rs::core::kMetersPerSecondToKilometersPerHour;
    train.kin.targetSpeed = trainJson.value("targetSpeed", 0.0) / rs::core::kMetersPerSecondToKilometersPerHour;
    train.kin.started = trainJson.value("started", false);
    if (trainJson.contains("stopDistance") && !trainJson["stopDistance"].is_null())
        train.kin.stopDistance = trainJson.value("stopDistance", 0.0);
    train.kin.arrivedAtStop = trainJson.value("arrivedAtStop", false);
    train.lastTravelReversed = trainJson.value("lastTravelReversed", false);
    train.stopTargetReversed = trainJson.value("stopTargetReversed", false);
    train.targetPlatform = rs::world::GuidFromJson(trainJson.value("targetPlatform", nlohmann::json()));
    train.departedPlatform = rs::world::GuidFromJson(trainJson.value("departedPlatform", nlohmann::json()));
    parsedTrain = train;
    if (trainJson.contains("schedule")) {
        const nlohmann::json& diaJson = trainJson["schedule"];
        if (!diaJson.is_object() || !diaJson.contains("phase") || !diaJson.contains("action")) return false;
        rs::sim::DiaRuntimeGroupSnapshot dia;
        dia.trainId = train.trainId;
        if (!DiaStateFromJson(diaJson["phase"], dia.state) ||
            !DiaActionFromJson(diaJson["action"], dia.currentAction)) return false;
        dia.phaseElapsed = diaJson.value("phaseElapsed", 0.0);
        dia.visitPlatform = rs::world::GuidFromJson(diaJson.value("visitPlatform", nlohmann::json()));
        dia.departedPlatform = rs::world::GuidFromJson(diaJson.value("departedPlatform", nlohmann::json()));
        dia.departureScheduled = diaJson.value("departureScheduled", false);
        dia.departureAbsSeconds = diaJson.value("departureAt", 0.0);
        dia.doorCloseAbsSeconds = diaJson.value("doorCloseAt", 0.0);
        parsedDia = std::move(dia);
    }
    return true;
}
}  // namespace

bool RslRuntimeStateFromJson(
    const nlohmann::json& json, const RslTrainHeads& heads,
    RslRuntimeState& outState, size_t* skippedTrains) {
    // "trains"は空配列だと保存時にRemoveEmptyJsonMembers()でキーごと省略される
    // (空コンテナは省略する規則)。キーが無いときは「列車0件」として扱う。
    const nlohmann::json trains = json.is_object() ? json.value("trains", nlohmann::json::array()) : nlohmann::json{};
    if (!json.is_object() || json.value("version", 0) != 1 || !trains.is_array()) return false;
    RslRuntimeState parsed;
    for (const nlohmann::json& trainJson : trains) {
        // 1本が読めなくても、残りの列車と線路はそのまま読む。
        rs::sim::TrainRuntimeSnapshot train;
        std::optional<rs::sim::DiaRuntimeGroupSnapshot> dia;
        bool ok = false;
        try {
            ok = TrainRuntimeFromJson(trainJson, heads, train, dia);
        } catch (const nlohmann::json::exception&) {
            ok = false;
        }
        if (!ok) {
            if (skippedTrains != nullptr) ++*skippedTrains;
            continue;
        }
        parsed.trains.push_back(std::move(train));
        if (dia) parsed.dia.groups.push_back(std::move(*dia));
    }
    if (json.contains("routineCursors") && !CursorsFromJson(json["routineCursors"], parsed.dia)) return false;
    outState = std::move(parsed);
    return true;
}

}  // namespace rs::app

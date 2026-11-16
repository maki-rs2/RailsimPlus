#pragma once

#include <unordered_map>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "TrainRunInfoRegistry.h"

namespace rs::world {

struct TrainRunInfoAssignment {
    TrainId trainId = NilGuid();
    TrainRunInfo info;
};

struct TrainRunInfoRegistrySnapshot {
    std::vector<TrainRunInfoAssignment> assignments;
};

nlohmann::json TrainRunInfoRegistryToJson(
    const std::unordered_map<TrainId, TrainRunInfo, GuidHash>& assignments);
TrainRunInfoRegistrySnapshot TrainRunInfoRegistryFromJson(const nlohmann::json& json);

}  // namespace rs::world

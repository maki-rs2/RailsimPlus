#pragma once

#include <vector>

#include <nlohmann/json.hpp>

#include "TrainTagRegistry.h"

namespace rs::world {

struct FormationTagAssignment {
    Guid formationId = NilGuid();
    std::vector<int> tagIds;
};

struct TrainFormationLink {
    TrainId trainId = NilGuid();
    Guid formationId = NilGuid();
};

struct TrainTagRegistrySnapshot {
    std::vector<TrainTagDef> tags;
    std::vector<FormationTagAssignment> formationTags;
    std::vector<TrainFormationLink> trainFormations;
};

// 保存形式: { tags:[{id,label,color}], formations:[{formation,tagIds}], trainFormations:[{train,formation}] }。
// 列車ごとのタグ(旧形式の assignments)は読まない。
nlohmann::json TrainTagRegistryToJson(const TrainTagRegistry& registry);
TrainTagRegistrySnapshot TrainTagRegistryFromJson(const nlohmann::json& json);
TrainTagRegistrySnapshot TrainTagRegistrySnapshotOf(const TrainTagRegistry& registry);

}  // namespace rs::world

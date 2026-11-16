#include "TrainTagRegistrySerialize.h"

#include <spdlog/spdlog.h>

#include "../alignment/GuidJson.h"

namespace rs::world {
namespace {

nlohmann::json ColorToJson(const rs::core::Color& color) {
    return nlohmann::json::array({ color.r, color.g, color.b, color.a });
}

rs::core::Color ColorFromJson(const nlohmann::json& json) {
    rs::core::Color color;
    if (!json.is_array() || json.size() != 4) {
        spdlog::warn("[train_tag_registry_serialize] color is invalid; using the default value.");
        return color;
    }
    try {
        const float r = json[0].get<float>();
        const float g = json[1].get<float>();
        const float b = json[2].get<float>();
        const float a = json[3].get<float>();
        color = rs::core::Color(r, g, b, a);
    } catch (const nlohmann::json::exception&) {
        spdlog::warn("[train_tag_registry_serialize] color is invalid; using the default value.");
    }
    return color;
}

}  // namespace

nlohmann::json TrainTagRegistryToJson(const TrainTagRegistry& registry) {
    nlohmann::json tagJson = nlohmann::json::array();
    for (const TrainTagDef& tag : registry.Tags()) {
        tagJson.push_back({ { "id", tag.id }, { "label", tag.label },
                            { "color", ColorToJson(tag.color) } });
    }
    nlohmann::json formationJson = nlohmann::json::array();
    for (const std::pair<const Guid, std::vector<int>>& entry : registry.FormationTagAssignments()) {
        formationJson.push_back(
            { { "formation", GuidToJson(entry.first) }, { "tagIds", entry.second } });
    }
    nlohmann::json linkJson = nlohmann::json::array();
    for (const std::pair<const TrainId, Guid>& link : registry.TrainFormationLinks()) {
        linkJson.push_back(
            { { "train", GuidToJson(link.first) }, { "formation", GuidToJson(link.second) } });
    }
    return { { "tags", std::move(tagJson) }, { "formations", std::move(formationJson) },
             { "trainFormations", std::move(linkJson) } };
}

TrainTagRegistrySnapshot TrainTagRegistrySnapshotOf(const TrainTagRegistry& registry) {
    TrainTagRegistrySnapshot snapshot;
    snapshot.tags = registry.Tags();
    for (const std::pair<const Guid, std::vector<int>>& entry : registry.FormationTagAssignments()) {
        snapshot.formationTags.push_back({ entry.first, entry.second });
    }
    for (const std::pair<const TrainId, Guid>& link : registry.TrainFormationLinks()) {
        snapshot.trainFormations.push_back({ link.first, link.second });
    }
    return snapshot;
}

TrainTagRegistrySnapshot TrainTagRegistryFromJson(const nlohmann::json& json) {
    TrainTagRegistrySnapshot result;
    if (!json.is_object()) {
        spdlog::warn("[train_tag_registry_serialize] trainTags is not an object; skipping it.");
        return result;
    }
    const nlohmann::json tags = json.value("tags", nlohmann::json::array());
    if (!tags.is_array()) {
        spdlog::warn("[train_tag_registry_serialize] tags is not an array; skipping it.");
    } else {
        for (const nlohmann::json& item : tags) {
            if (!item.is_object()) {
                spdlog::warn("[train_tag_registry_serialize] tag is not an object; skipping it.");
                continue;
            }
            try {
                if (!item.contains("id") || !item.contains("label")) {
                    spdlog::warn("[train_tag_registry_serialize] tag is incomplete; skipping it.");
                    continue;
                }
                TrainTagDef tag;
                tag.id = item["id"].get<int>();
                tag.label = item["label"].get<std::string>();
                tag.color = ColorFromJson(item.value("color", nlohmann::json::array()));
                result.tags.push_back(std::move(tag));
            } catch (const nlohmann::json::exception&) {
                spdlog::warn("[train_tag_registry_serialize] tag is invalid; skipping it.");
            }
        }
    }
    const nlohmann::json formations = json.value("formations", nlohmann::json::array());
    if (!formations.is_array()) {
        spdlog::warn("[train_tag_registry_serialize] formations is not an array; skipping it.");
    } else {
        for (const nlohmann::json& item : formations) {
            try {
                if (!item.is_object() || !item.contains("formation") || !item.contains("tagIds")) {
                    spdlog::warn("[train_tag_registry_serialize] formation tags are incomplete; skipping them.");
                    continue;
                }
                result.formationTags.push_back(
                    { GuidFromJson(item["formation"]), item["tagIds"].get<std::vector<int>>() });
            } catch (const nlohmann::json::exception&) {
                spdlog::warn("[train_tag_registry_serialize] formation tags are invalid; skipping them.");
            }
        }
    }
    const nlohmann::json links = json.value("trainFormations", nlohmann::json::array());
    if (!links.is_array()) {
        spdlog::warn("[train_tag_registry_serialize] trainFormations is not an array; skipping it.");
        return result;
    }
    for (const nlohmann::json& item : links) {
        try {
            if (!item.is_object() || !item.contains("train") || !item.contains("formation")) {
                spdlog::warn("[train_tag_registry_serialize] train link is incomplete; skipping it.");
                continue;
            }
            result.trainFormations.push_back(
                { GuidFromJson(item["train"]), GuidFromJson(item["formation"]) });
        } catch (const nlohmann::json::exception&) {
            spdlog::warn("[train_tag_registry_serialize] train link is invalid; skipping it.");
        }
    }
    return result;
}

}  // namespace rs::world

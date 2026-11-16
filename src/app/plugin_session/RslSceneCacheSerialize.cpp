#include "RslSceneCacheSerialize.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "../camera/FlyCameraSerialize.h"
#include "../../world/alignment/TrackGraphSerialize.h"
#include "../../world/alignment/GuidJson.h"
#include "../../world/alignment/JsonReadValue.h"
#include "../../world/scene/ManualPlacementOverlaySerialize.h"
#include "../../world/scene/SceneEntitySerialize.h"

namespace rs::app {
namespace {

rs::core::Vec3 Vec3FromJson(const nlohmann::json& json) {
    if (!json.is_array() || json.size() != 3) {
        return {};
    }
    return { json[0].get<float>(), json[1].get<float>(), json[2].get<float>() };
}

void AppendSceneryPlacements(const std::vector<rs::world::SceneEntity>& entities,
                             const PluginTypeClassifier& classifier, nlohmann::json& structs,
                             nlohmann::json& stations, nlohmann::json& scenery) {
    std::unordered_map<std::string, rs::legacy_pi::PluginType> typesByFolder;
    for (const rs::world::SceneEntity& entity : entities) {
        if (entity.category != rs::world::SceneEntityCategory::Scenery) {
            continue;
        }
        const std::pair<std::unordered_map<std::string, rs::legacy_pi::PluginType>::iterator, bool>
            insertion = typesByFolder.emplace(entity.folderUtf8, rs::legacy_pi::PluginType::Unknown);
        if (insertion.second) {
            insertion.first->second = classifier(entity.folderUtf8);
        }
        const nlohmann::json json = rs::world::SceneEntityToJson(entity);
        if (insertion.first->second == rs::legacy_pi::PluginType::Struct) {
            structs.push_back(json);
        } else if (insertion.first->second == rs::legacy_pi::PluginType::Station) {
            stations.push_back(json);
        } else {
            // 種別を判定できない配置(地表面プラグインや定義の読取失敗)も捨てずに
            // 汎用バケットへ保存する。警告はfolder単位で1回だけ出す(取り込みレイアウトでは
            // 同一folderの配置が数百件規模になり、個体ごとに出すとログが埋まるため)。
            if (insertion.second) {
                spdlog::warn("[rsl_layout_serialize] scenery plugin '{}' is not a struct or station; "
                             "saving it as a generic scenery entry.",
                             entity.folderUtf8);
            }
            scenery.push_back(json);
        }
    }
}

void AddSceneryPlacements(const nlohmann::json& placements, const char* key,
                          std::vector<rs::world::SceneEntity>& manualScenery) {
    if (!placements.contains(key) || !placements[key].is_array()) {
        return;
    }
    for (const nlohmann::json& item : placements[key]) {
        rs::world::SceneEntity entity = rs::world::SceneEntityFromJson(item);
        entity.category = rs::world::SceneEntityCategory::Scenery;
        manualScenery.push_back(std::move(entity));
    }
}

nlohmann::json WireSpansToJson(const std::vector<ImportedWireSpan>& wireSpans) {
    nlohmann::json result = nlohmann::json::array();
    for (const ImportedWireSpan& wireSpan : wireSpans) {
        result.push_back({ { "posA", rs::world::Vec3ToJson(wireSpan.posA) },
                           { "posB", rs::world::Vec3ToJson(wireSpan.posB) },
                           { "plugin", wireSpan.lineFolderUtf8 } });
    }
    return result;
}

void RestoreWireSpans(const nlohmann::json& scene, std::vector<ImportedWireSpan>& wireSpans) {
    if (!scene.contains("wireSpans") || !scene["wireSpans"].is_array()) {
        return;
    }
    for (const nlohmann::json& item : scene["wireSpans"]) {
        if (!item.is_object()) {
            continue;
        }
        ImportedWireSpan wireSpan;
        if (item.contains("posA")) wireSpan.posA = Vec3FromJson(item["posA"]);
        if (item.contains("posB")) wireSpan.posB = Vec3FromJson(item["posB"]);
        wireSpan.lineFolderUtf8 = item.value("plugin", std::string{});
        wireSpans.push_back(std::move(wireSpan));
    }
}

// 番線の rs2Address を、stationEntity が指す駅配置の値から求める。配置が無ければ nullopt。
std::optional<std::string> DerivedPlatformAddress(const rs::world::PlatformStopInfo& platform,
                                                  const std::vector<rs::world::SceneEntity>& entities) {
    if (rs::world::GuidIsNil(platform.stationEntity)) return std::nullopt;
    for (const rs::world::SceneEntity& entity : entities) {
        if (entity.guid == platform.stationEntity) return entity.sourceAddress;
    }
    return std::nullopt;
}

// 駅の rs2Address を、その駅の番線のうち値が求まる最初の番線の値から求める。無ければ nullopt。
std::optional<std::string> DerivedStationAddress(const rs::world::TrackGraph& graph, const rs::world::Guid& stationId,
                                                 const std::vector<rs::world::SceneEntity>& entities) {
    for (const rs::world::PlatformStopInfo& platform : graph.platforms) {
        if (platform.stationId != stationId) continue;
        if (std::optional<std::string> address = DerivedPlatformAddress(platform, entities)) return address;
    }
    return std::nullopt;
}

// 求め直せる rs2Address を trackGraph の JSON から消す(配列の並びは graph.platforms / graph.stations と同じ)。
void EraseDerivedAddresses(nlohmann::json& graphJson, const rs::world::TrackGraph& graph,
                           const std::vector<rs::world::SceneEntity>& entities) {
    const auto erase = [&graphJson](const char* key, size_t index) {
        if (graphJson.contains(key) && graphJson[key].is_array() && index < graphJson[key].size()) {
            graphJson[key][index].erase("rs2Address");
        }
    };
    for (size_t index = 0; index < graph.platforms.size(); ++index) {
        if (DerivedPlatformAddress(graph.platforms[index], entities) == graph.platforms[index].stationAddress) {
            erase("platforms", index);
        }
    }
    for (size_t index = 0; index < graph.stations.size(); ++index) {
        if (DerivedStationAddress(graph, graph.stations[index].id, entities) == graph.stations[index].sourceAddress) {
            erase("stations", index);
        }
    }
}

// rs2Address を保存していない番線・駅へ、駅配置から求めた値を入れる。
void FillDerivedAddresses(const nlohmann::json& graphJson, rs::world::TrackGraph& graph,
                          const std::vector<rs::world::SceneEntity>& entities) {
    const auto missing = [&graphJson](const char* key, size_t index) {
        return graphJson.contains(key) && graphJson[key].is_array() && index < graphJson[key].size() &&
               graphJson[key][index].is_object() && !graphJson[key][index].contains("rs2Address");
    };
    for (size_t index = 0; index < graph.platforms.size(); ++index) {
        if (!missing("platforms", index)) continue;
        if (std::optional<std::string> address = DerivedPlatformAddress(graph.platforms[index], entities)) {
            graph.platforms[index].stationAddress = std::move(*address);
        }
    }
    for (size_t index = 0; index < graph.stations.size(); ++index) {
        if (!missing("stations", index)) continue;
        if (std::optional<std::string> address = DerivedStationAddress(graph, graph.stations[index].id, entities)) {
            graph.stations[index].sourceAddress = std::move(*address);
        }
    }
}

}  // namespace

nlohmann::json SceneCacheToJson(const LayoutSceneCache& cache,
                                const PluginTypeClassifier& classifier) {
    nlohmann::json structs = nlohmann::json::array();
    nlohmann::json stations = nlohmann::json::array();
    nlohmann::json scenery = nlohmann::json::array();
    AppendSceneryPlacements(cache.manualScenery, classifier, structs, stations, scenery);
    nlohmann::json placements = rs::world::OverlayToJson(cache.overlay);
    if (!structs.empty()) placements["structures"] = std::move(structs);
    if (!stations.empty()) placements["stationPlugins"] = std::move(stations);
    if (!scenery.empty()) placements["scenery"] = std::move(scenery);
    nlohmann::json graphJson = rs::world::TrackGraphToJson(cache.graph);
    EraseDerivedAddresses(graphJson, cache.graph, cache.manualScenery);
    nlohmann::json scene = { { "guid", rs::world::GuidToJson(rs::world::GuidIsNil(cache.guid) ? rs::world::GenerateGuid() : cache.guid) },
                             { "trackGraph", std::move(graphJson) },
                             { "placements", std::move(placements) } };
    if (!cache.wireSpans.empty()) scene["wireSpans"] = WireSpansToJson(cache.wireSpans);
    if (!cache.name.empty()) {
        scene["name"] = cache.name;
    }
    if (!cache.envPlugin.empty()) {
        // "plugin"/"folders"はプラグインのパス参照として書き換えられる予約キーなので使わない。
        scene["envPlugin"] = cache.envPlugin;
    }
    if (cache.hasCamera) {
        scene["camera"] = FlyCameraToJson(cache.camera);
    }
    return scene;
}

void RestoreSceneCache(const nlohmann::json& scene, LayoutSceneCache& cache) {
    const nlohmann::json placements = scene.value("placements", nlohmann::json::object());
    cache.graph = rs::world::TrackGraphFromJson(scene.value("trackGraph", nlohmann::json::object()));
    cache.guid = rs::world::GuidFromJson(scene.value("guid", nlohmann::json{}));
    cache.overlay = rs::world::OverlayFromJson(placements);
    cache.name = scene.value("name", std::string{});
    cache.envPlugin = scene.value("envPlugin", std::string{});
    if (scene.contains("camera")) {
        cache.camera = FlyCameraFromJson(scene.at("camera"));
        cache.hasCamera = true;
    }
    AddSceneryPlacements(placements, "structures", cache.manualScenery);
    AddSceneryPlacements(placements, "stationPlugins", cache.manualScenery);
    AddSceneryPlacements(placements, "scenery", cache.manualScenery);
    if (scene.contains("trackGraph")) FillDerivedAddresses(scene["trackGraph"], cache.graph, cache.manualScenery);
    RestoreWireSpans(scene, cache.wireSpans);
}

size_t CountPlacementItems(const rs::world::ManualPlacementOverlay& overlay,
                           const nlohmann::json& placements) {
    size_t count = overlay.poles.size() + overlay.piers.size() + overlay.lines.size();
    for (const char* key : { "structures", "stationPlugins", "scenery" }) {
        if (placements.contains(key) && placements[key].is_array()) {
            count += placements[key].size();
        }
    }
    return count;
}

}  // namespace rs::app

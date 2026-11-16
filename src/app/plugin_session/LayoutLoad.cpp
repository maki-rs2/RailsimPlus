#include "LayoutLoad.h"

#include <algorithm>
#include <optional>
#include <unordered_set>

#include <spdlog/spdlog.h>

#include "../../editor/tool/Editor.h"
#include "FormationBuilder.h"
#include "PluginSelector.h"
#include "RailTieHeights.h"
#include "../camera/CameraControl.h"
#include "../ui/FormationPanel.h"

#include "../scene/SceneRenderer.h"
#include "../../world/alignment/GuidJson.h"
#include "../../world/alignment/FormationRoutineResolve.h"
#include "../../world/scene/SceneRegistry.h"
#include "../../world/time/GameClockSerialize.h"
#include "../../world/time/SolarModeNames.h"
#include "../../world/time/SolarPosition.h"

namespace rs::app {
namespace {

bool HasFormation(const PluginSelector& selector, const rs::world::Guid& formationId) {
    return std::any_of(selector.Formations().begin(), selector.Formations().end(),
                       [&formationId](const Formation& formation) { return formation.guid == formationId; });
}

void RestoreTrainTags(const rs::world::TrainTagRegistrySnapshot& snapshot, const PluginSelector& selector,
                      rs::world::TrainTagRegistry& tagRegistry) {
    tagRegistry.ClearTags();
    for (const rs::world::TrainTagDef& tag : snapshot.tags) tagRegistry.RestoreTag(tag);
    for (const rs::world::FormationTagAssignment& entry : snapshot.formationTags) {
        if (HasFormation(selector, entry.formationId)) tagRegistry.SetFormationTags(entry.formationId, entry.tagIds);
    }
    for (const rs::world::TrainFormationLink& link : snapshot.trainFormations) {
        if (HasFormation(selector, link.formationId)) tagRegistry.LinkTrainToFormation(link.trainId, link.formationId);
    }
}

void ApplyFormationsFromJson(const nlohmann::json& defs, const nlohmann::json& selectedFormation,
                             PluginSelector& selector, rs::world::TrainTagRegistry& tagRegistry) {
    if (!defs.is_array()) {
        spdlog::warn("[rsl_layout_serialize] formations is not an array; skipping it.");
        return;
    }

    // 差し替える編成の登録(タグ・配置済み列車との対応)を先に落とす。
    while (!selector.Formations().empty()) {
        tagRegistry.RemoveFormation(selector.Formations().front().guid);
        selector.RemoveFormation(0);
    }
    for (const nlohmann::json& defJson : defs) {
        const std::string name = defJson.value("name", std::string{});
        const rs::world::Guid guid = rs::world::GuidFromJson(defJson.value("guid", nlohmann::json{}));
        const size_t defIndex = selector.AddFormation(name, guid);
        const nlohmann::json cars = defJson.value("cars", nlohmann::json::array());
        if (!cars.is_array()) {
            spdlog::warn("[rsl_layout_serialize] train definition '{}' has non-array cars; skipping them.", name);
            continue;
        }
        for (const nlohmann::json& carJson : cars) {
            const std::string plugin = carJson.at("plugin").get<std::string>();
            std::vector<int32_t> switchOptions;
            if (carJson.contains("switchOptions")) {
                switchOptions = carJson.at("switchOptions").get<std::vector<int32_t>>();
            }
            std::vector<std::string> carWarnings;
            AddFormationCar(selector, defIndex, plugin, carJson.value("reversed", false), switchOptions,
                            carWarnings);
            for (const std::string& warning : carWarnings) {
                spdlog::warn("[rsl_layout_serialize] {}", warning);
            }
        }
    }
    // 選択中の編成はGUIDで保存されるため、復元後の並びから添字を引き直す。
    const rs::world::Guid selectedGuid = rs::world::GuidFromJson(selectedFormation);
    for (size_t index = 0; index < selector.Formations().size(); ++index) {
        if (selector.Formations()[index].guid == selectedGuid) {
            selector.SelectFormation(index);
            break;
        }
    }
}

}  // namespace

nlohmann::json FormationsToJson(const PluginSelector& selector, size_t firstIndex) {
    nlohmann::json defs = nlohmann::json::array();
    for (size_t index = firstIndex; index < selector.Formations().size(); ++index) {
        const Formation& def = selector.Formations()[index];
        nlohmann::json cars = nlohmann::json::array();
        for (const FormationCar& car : def.cars) {
            nlohmann::json carJson = {
                { "plugin", car.folderUtf8 },
                { "reversed", car.reversed },
            };
            if (!car.switchOptions.empty()) {
                carJson["switchOptions"] = car.switchOptions;
            }
            cars.push_back(std::move(carJson));
        }
        defs.push_back({ { "guid", rs::world::GuidToJson(def.guid) }, { "name", def.name }, { "cars", std::move(cars) } });
    }
    return defs;
}

void RemoveFormationsFrom(PluginSelector& selector, size_t firstIndex) {
    while (selector.Formations().size() > firstIndex) selector.RemoveFormation(selector.Formations().size() - 1);
}

nlohmann::json LayoutTimeToJson(const rs::world::GameClock& clock, const rs::world::SolarOverrides& overrides) {
    nlohmann::json time = rs::world::GameClockToJson(clock);
    time["earthRotation"] = std::string(rs::world::EarthRotationModeKey(overrides.earthRotationMode));
    time["earthRevolution"] = std::string(rs::world::EarthRevolutionModeKey(overrides.earthRevolutionMode));
    return time;
}

void LayoutTimeFromJson(const nlohmann::json& json, rs::world::GameClock& clock,
                        rs::world::SolarOverrides& overrides) {
    rs::world::ApplyGameClockFromJson(json, clock);
    if (!json.is_object()) return;
    const std::string earthRotation = json.value("earthRotation", std::string{});
    if (const std::optional<rs::world::EarthRotationMode> mode = rs::world::ParseEarthRotationMode(earthRotation)) {
        overrides.earthRotationMode = *mode;
    }
    const std::string earthRevolution = json.value("earthRevolution", std::string{});
    if (const std::optional<rs::world::EarthRevolutionMode> mode =
            rs::world::ParseEarthRevolutionMode(earthRevolution)) {
        overrides.earthRevolutionMode = *mode;
    }
}

void ResetLiveLayoutState(rs::world::SceneRegistry& sceneRegistry, SceneRenderer& sceneRenderer) {
    sceneRegistry.RemoveByCategory(rs::world::SceneEntityCategory::Scenery);
    sceneRegistry.RemoveByCategory(rs::world::SceneEntityCategory::Car);
    sceneRegistry.RemoveByCategory(rs::world::SceneEntityCategory::TrackPlugin);
    sceneRenderer.ResetForNewLayout();
}

LayoutActionResult ApplyNormalizedLayout(NormalizedLayout&& layout, const LayoutApplyRefs& refs) {
    LayoutActionResult result;
    result.warnings = std::move(layout.warnings);
    ResetLiveLayoutState(refs.sceneRegistry, refs.sceneRenderer);
    std::unordered_set<std::string> loadedFamilies;
    for (const LayoutSceneCache& cache : layout.scenes) {
        for (const std::pair<const rs::world::Guid, rs::world::TrackEdge>& entry : cache.graph.edges) {
            for (const std::string& folder : rs::world::ActiveFolders(entry.second)) {
                if (!loadedFamilies.insert(folder).second) continue;
                if (layout.reportedTrackPluginFailures.count(folder) != 0) continue;
                std::string error;
                const std::optional<std::string> registered = refs.selector.EnsureTrackPluginLoaded(folder, error);
                if (registered) {
                    loadedFamilies.insert(*registered);
                } else {
                    spdlog::warn("[layout] could not restore rail plugin '{}': {}", folder, error);
                }
            }
        }
    }
    for (LayoutSceneCache& cache : layout.scenes) {
        for (auto& [guid, edge] : cache.graph.edges) {
            (void)guid;
            refs.selector.CanonicalizeTrackPluginFolders(edge);
        }
    }
    for (std::string& folder : layout.layingSettings.defaultTrackPlugins)
        folder = refs.selector.RegisteredTrackPluginFolder(folder);
    //列車の最初の姿勢計算より前に、全Sceneのedgeへレール頭頂面の持ち上げ量を入れる
    for (LayoutSceneCache& cache : layout.scenes) UpdateEdgeRailTopLifts(cache.graph, refs.selector.TrackPluginEntries());
    if (refs.beforeActiveSceneApply) refs.beforeActiveSceneApply();
    refs.session.scenes = std::move(layout.scenes);
    refs.session.activeIndex = std::min(layout.activeIndex, refs.session.scenes.size() - 1);
    refs.session.warpLinks = std::move(layout.warpLinks);
    refs.editor.layingSettings = std::move(layout.layingSettings);
    for (rs::world::SceneEntity& entity : layout.cars) refs.sceneRegistry.RestoreEntity(std::move(entity));
    if (refs.trainPlacementUi != nullptr) {
        // 前のレイアウトの編成を残さない。
        refs.trainPlacementUi->placedTrainIds.clear();
        refs.trainPlacementUi->focusedTrainId = rs::world::NilGuid();
        refs.trainPlacementUi->placedTrainIds.insert(refs.trainPlacementUi->placedTrainIds.end(),
                                                        layout.placedTrainIds.begin(),
                                                        layout.placedTrainIds.end());
    }
    std::vector<ImportedWireSpan> discardedWireSpans;
    ApplySceneCacheToLive(refs.session.scenes[refs.session.activeIndex], refs.editor, refs.sceneRegistry,
                          refs.camera, refs.importedWireSpans != nullptr ? *refs.importedWireSpans : discardedWireSpans);
    for (const rs::world::TrainRunInfoAssignment& assignment : layout.trainRunInfo.assignments) refs.sceneRenderer.SetTrainRunInfo(assignment.trainId, assignment.info);
    refs.sceneRenderer.TrainRuntime().Restore(layout.runtime.trains);
    refs.sceneRenderer.DiaRuntime().Restore(layout.runtime.dia);
    for (const TrainSpeedSeed& seed : layout.trainSpeedSeeds) { refs.sceneRenderer.SetGroupTargetSpeed(seed.trainId, seed.targetKmh); refs.sceneRenderer.SetGroupCurrentSpeed(seed.trainId, seed.currentKmh); }
    refs.gameClock = layout.gameClock;
    refs.solarOverrides = layout.solarOverrides;
    ApplyFormationsFromJson(layout.formationDefsJson, layout.selectedFormationJson, refs.selector,
                            refs.sceneRenderer.DiaRuntime().TagRegistry());
    // 編成の定義の差し替えより後に、タグと列車の対応を戻す(存在しない編成の分は捨てる)。
    RestoreTrainTags(layout.trainTags, refs.selector, refs.sceneRenderer.DiaRuntime().TagRegistry());
    const rs::world::TrainTagRegistry& tagRegistry = refs.sceneRenderer.DiaRuntime().TagRegistry();
    int migratedRoutines = rs::world::MigrateTrainRoutinesToFormations(refs.editor.graph, tagRegistry);
    for (LayoutSceneCache& scene : refs.session.scenes) {
        migratedRoutines += rs::world::MigrateTrainRoutinesToFormations(scene.graph, tagRegistry);
    }
    if (migratedRoutines > 0) {
        spdlog::info("[rsl_layout_serialize] 列車別ルーチンを編成別ルーチンへ{}件移行しました",
                     migratedRoutines);
    }
    result.nodeCount = refs.editor.graph.nodes.size();
    result.edgeCount = refs.editor.graph.edges.size();
    result.sceneCount = refs.session.scenes.size();
    return result;
}

}  // namespace rs::app

// RslLayoutSerialize.cpp - .rsl レイアウト保存処理
#include "RslLayoutSerialize.h"
#include "LayoutLoad.h"

#include <array>
#include <filesystem>
#include <utility>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "../../editor/tool/Editor.h"
#include "../../platform/file/AtomicFileWrite.h"
#include "../../platform/text/Encoding.h"
#include "../../plugins/load/PluginDefinitionLookup.h"
#include "../../world/alignment/WarpLinkTableSerialize.h"
#include "../../world/alignment/AlignmentEnumJson.h"
#include "../../world/alignment/GuidJson.h"
#include "../../world/scene/CarEntitySerialize.h"
#include "../../world/scene/TrainRunInfoRegistrySerialize.h"
#include "../../world/scene/TrainTagRegistrySerialize.h"
#include "../../world/scene/ManualPlacementOverlaySerialize.h"
#include "../../world/scene/SceneEntitySerialize.h"
#include "../../world/scene/SceneRegistry.h"
#include "../../world/time/GameClockSerialize.h"
#include "../../world/time/SolarModeNames.h"
#include "../camera/FlyCameraSerialize.h"
#include "../scene/SceneRenderer.h"
#include "LayoutSession.h"
#include "PluginRootResolver.h"
#include "PluginSelector.h"
#include "RslJsonWriter.h"
#include "RslSceneCacheSerialize.h"
#include "RslRuntimeState.h"
#include "../../plugins/schema/PluginSchema.h"

namespace rs::app {
namespace {

nlohmann::json CarsToJson(const rs::world::SceneRegistry& sceneRegistry) {
    nlohmann::json cars = nlohmann::json::array();
    for (const rs::world::SceneEntity& entity : sceneRegistry.Entities()) {
        if (entity.category == rs::world::SceneEntityCategory::Car && rs::world::IsUserPlacement(entity)) {
            rs::world::SceneEntity saved = entity;
            saved.switchOptions = sceneRegistry.SavedSwitchOptions(entity);
            // 車両の Scene は保存しない(読込で、edge を持つ Scene から求める)。
            cars.push_back(rs::world::CarEntityToJson(saved));
        }
    }
    return cars;
}

// ワープの Scene 番号を Scene の GUID へ置き換える。範囲外の番号は、読込で全体の失敗になるnullにせず、先頭のSceneへ
// 寄せて保存する(先頭は、読込でactiveSceneが無いときの既定のScene)。寄せた件数を返す。
size_t ReplaceSceneIndicesWithGuids(nlohmann::json& document, const std::vector<LayoutSceneCache>& scenes) {
    size_t repairedCount = 0;
    const auto sceneGuid = [&scenes, &repairedCount](size_t index) -> nlohmann::json {
        if (scenes.empty()) return nlohmann::json(nullptr);
        if (index >= scenes.size()) {
            ++repairedCount;
            index = 0;
        }
        return rs::world::GuidToJson(scenes[index].guid);
    };
    nlohmann::json& world = document["world"];
    for (nlohmann::json& warp : world["warps"]) {
        warp["sceneA"] = sceneGuid(warp.value("sceneA", size_t{0}));
        warp["sceneB"] = sceneGuid(warp.value("sceneB", size_t{0}));
    }
    return repairedCount;
}


// 太陽オーバーライド(自転/公転の強制モード)。緯度・地軸傾斜・夜判定閾値は開発側が決める
// 埋め込みプリセット(EnvironmentPresets)側の値で、レイアウトごとに変わらないため保存しない。
nlohmann::json LayingSettingsToJson(const rs::editor::TrackLayingSettings& settings) {
    return {
        { "angleSnapAlways", settings.angleSnapEnabled },
        { "angleSnap", settings.angleSnapDeg },
        { "trackCount", settings.trackCount },
        { "trackSpacing", settings.trackSpacing },
        { "transitionEnabled", settings.transEnabled },
        { "transitionShape", rs::world::TransitionShapeToJson(settings.transShape) },
        { "transitionLength", settings.transLen },
        { "defaultPlugins", settings.defaultTrackPlugins },
    };
}

LayoutSceneCache BuildLiveSceneCache(const rs::editor::Editor& editor,
                                     const rs::world::SceneRegistry& sceneRegistry,
                                     const CameraController& camera, const rs::world::Guid& guid) {
    LayoutSceneCache cache;
    cache.guid = guid;
    cache.graph = editor.graph;
    cache.overlay = editor.manualOverlay;
    cache.camera = camera.fly;
    cache.hasCamera = true;
    for (const rs::world::SceneEntity& entity : sceneRegistry.Entities()) {
        if (entity.category == rs::world::SceneEntityCategory::Scenery && rs::world::IsUserPlacement(entity)) {
            cache.manualScenery.push_back(entity);
            cache.manualScenery.back().switchOptions = sceneRegistry.SavedSwitchOptions(entity);
        }
    }
    return cache;
}

void RemoveEmptyJsonMembers(nlohmann::json& json, bool preserveTopLevelMembers = false) {
    if (json.is_array()) {
        for (nlohmann::json& element : json) {
            RemoveEmptyJsonMembers(element);
        }
        return;
    }
    if (!json.is_object()) return;

    for (nlohmann::json::iterator iterator = json.begin(); iterator != json.end();) {
        RemoveEmptyJsonMembers(iterator.value());
        const bool keep = preserveTopLevelMembers &&
            (iterator.key() == "info" || iterator.key() == "world" ||
             iterator.key() == "session" || iterator.key() == "editor");
        if (!keep && (iterator.value().is_array() || iterator.value().is_object()) &&
            iterator.value().empty()) {
            iterator = json.erase(iterator);
        } else {
            ++iterator;
        }
    }
}

}  // namespace

const std::vector<std::string>& EmptyPluginRoots() {
    static const std::vector<std::string> kRoots;
    return kRoots;
}

rs::legacy_pi::PluginType DefaultClassifyPluginType(const std::string& folderUtf8) {
    try {
        const rs::legacy_pi::ParsedDefinitionResult result =
            rs::legacy_plugin::LoadPluginDefinition(folderUtf8);
        if (result.ok) {
            return result.def.header.type;
        }
    } catch (const std::exception& exception) {
        spdlog::warn("[rsl_layout_serialize] could not classify '{}': {}", folderUtf8, exception.what());
    }
    return rs::legacy_pi::PluginType::Unknown;
}

LayoutActionResult SaveRslLayout(const std::string& pathUtf8, const RslPersistenceRefs& refs) {
    return SaveRslLayout(pathUtf8, refs, DefaultClassifyPluginType);
}

LayoutActionResult SaveRslLayout(const std::string& pathUtf8, const RslPersistenceRefs& refs,
                            const PluginTypeClassifier& classifier) {
    LayoutSession& session = refs.session;
    rs::editor::Editor& editor = refs.editor;
    rs::world::SceneRegistry& sceneRegistry = refs.sceneRegistry;
    rs::world::GameClock& gameClock = refs.gameClock;
    PluginSelector& selector = refs.selector;
    SceneRenderer& sceneRenderer = refs.sceneRenderer;
    CameraController& camera = refs.camera;
    LayoutActionResult result;
    try {
        if (!classifier) {
            result = LayoutActionResult::Failure(LayoutActionStatus::ClassifierMissing, pathUtf8, "Plugin type classifier is not set.");
            return result;
        }
        CopyBackActiveScene(session, editor, sceneRegistry, camera, refs.importedWireSpans);
        std::vector<LayoutSceneCache> liveSceneCaches;
        std::vector<LayoutSceneCache>* sceneCaches = &session.scenes;
        if (sceneCaches->empty()) {
            if (rs::world::GuidIsNil(session.liveSceneGuid)) session.liveSceneGuid = rs::world::GenerateGuid();
            liveSceneCaches.push_back(BuildLiveSceneCache(editor, sceneRegistry, camera, session.liveSceneGuid));
            sceneCaches = &liveSceneCaches;
        }
        for (LayoutSceneCache& cache : *sceneCaches) {
            if (rs::world::GuidIsNil(cache.guid)) cache.guid = rs::world::GenerateGuid();
        }
        nlohmann::json scenes = nlohmann::json::array();
        for (const LayoutSceneCache& cache : *sceneCaches) {
            scenes.push_back(SceneCacheToJson(cache, classifier));
        }
        nlohmann::json cars = CarsToJson(sceneRegistry);
        nlohmann::json trainTags = rs::world::TrainTagRegistryToJson(sceneRenderer.DiaRuntime().TagRegistry());
        nlohmann::json trainRunInfo = rs::world::TrainRunInfoRegistryToJson(
            sceneRenderer.TrainRunInfoAssignments());
        const RslRuntimeState runtimeState{ sceneRenderer.TrainRuntime().Snapshot(),
                                            sceneRenderer.DiaRuntime().Snapshot() };
        const auto graphForRuntime = [&sceneCaches](size_t sceneIndex) -> const rs::world::TrackGraph* {
            return sceneIndex < sceneCaches->size() ? &(*sceneCaches)[sceneIndex].graph : nullptr;
        };
        const rs::world::Guid guid = refs.documentGuid == nullptr ? rs::world::GenerateGuid() : *refs.documentGuid;
        const rs::world::Guid activeScene = (*sceneCaches)[std::min(session.activeIndex, sceneCaches->size() - 1)].guid;
        nlohmann::json time = LayoutTimeToJson(gameClock, refs.solarOverrides);
        nlohmann::json document = {
            { "specVersion", 2 },
            { "type", "Layout" },
            { "info", { { "guid", rs::world::GuidToJson(guid) }, { "appVersion", RS_APP_VERSION } } },
            { "world", { { "activeScene", rs::world::GuidToJson(activeScene) }, { "scenes", std::move(scenes) },
                         { "cars", std::move(cars) },
                         { "warps", rs::world::WarpLinkTableToJson(session.warpLinks) },
                         { "time", std::move(time) },
                         { "formations", FormationsToJson(selector) },
                         { "trainTags", std::move(trainTags) },
                         { "trainRunInfo", std::move(trainRunInfo) } } },
            { "session", RslRuntimeStateToJson(runtimeState, graphForRuntime) },
            { "editor", { { "layingSettings", LayingSettingsToJson(editor.layingSettings) },
                            { "selectedFormation", selector.Formations().empty() ? nlohmann::json(nullptr) : rs::world::GuidToJson(selector.Formations()[selector.SelectedFormationIndex()].guid) } } },
        };
        const size_t repairedSceneRefs = ReplaceSceneIndicesWithGuids(document, *sceneCaches);
        if (repairedSceneRefs > 0) {
            const std::string warning = "存在しないSceneを指す参照が" + std::to_string(repairedSceneRefs) +
                                        "件あったため、先頭のSceneへ寄せて保存しました";
            spdlog::warn("[rsl_layout_serialize] {}", warning);
            result.warnings.push_back(warning);
        }
        RelativizeLayoutPluginReferences(document, refs.pluginRoots);
        RemoveEmptyJsonMembers(document, true);
        const std::filesystem::path path(rs::platform_text::Utf8ToWide(pathUtf8));
        // 一時ファイル経由で置き換える。書き込み中に落ちても既存のレイアウトは残る。
        std::string writeError;
        if (refs.thumbnailBase64 != nullptr) document["info"]["thumbnail"] = *refs.thumbnailBase64;
        if (!rs::platform_file::WriteFileAtomically(path, DumpRslJson(document), writeError)) {
            result = LayoutActionResult::Failure(LayoutActionStatus::FileWriteFailed, pathUtf8, writeError);
            return result;
        }
    } catch (const std::exception& exception) {
        result = LayoutActionResult::Failure(LayoutActionStatus::Unexpected, exception.what(), exception.what());
    }
    return result;
}

}  // namespace rs::app

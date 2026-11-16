// RslLayoutLoad.cpp - .rsl レイアウト読込処理
#include "RslLayoutLoad.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <stdexcept>
#include <unordered_set>
#include <utility>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "../../editor/tool/Editor.h"
#include "../../platform/text/Encoding.h"
#include "../../world/alignment/GuidJson.h"
#include "../../world/alignment/AlignmentEnumJson.h"
#include "../../world/alignment/TrackGraphSerialize.h"
#include "../../world/alignment/WarpLinkTableSerialize.h"
#include "../../world/scene/CarEntitySerialize.h"
#include "../../world/scene/TrainRunInfoRegistrySerialize.h"
#include "../../world/scene/TrainTagRegistrySerialize.h"
#include "../../world/scene/SceneRegistry.h"
#include "../../world/time/GameClockSerialize.h"
#include "../scene/TrainPlacement.h"
#include "../scene/ManualPlacementOverlayApply.h"
#include "../scene/StructLinePoleTracker.h"
#include "../scene/SceneRenderer.h"
#include "LayoutSession.h"
#include "LayoutLoad.h"
#include "PluginRootResolver.h"
#include "PluginSelector.h"
#include "RslSceneCacheSerialize.h"
#include "RslRuntimeState.h"

namespace rs::app {
namespace {

// Scene GUID解決失敗など、「読込全体を失敗させるべき不正」を表す。
// nlohmann::json::exceptionとは区別しつつ、DecodeRsl側でLayoutActionStatus::InvalidFormatへ
// 一律にマッピングするために専用の例外型を用意する。
class RslInvalidLayoutError : public std::runtime_error {
public:
    explicit RslInvalidLayoutError(const std::string& message) : std::runtime_error(message) {}
};

// 車両の edge を持つ Scene の添字。どの Scene にも無い、または複数の Scene にあるときは nullopt。
std::optional<size_t> SceneIndexOfEdge(const std::vector<LayoutSceneCache>& scenes, const rs::world::Guid& edge) {
    std::optional<size_t> found;
    for (size_t index = 0; index < scenes.size(); ++index) {
        if (!scenes[index].graph.edges.contains(edge)) continue;
        if (found) return std::nullopt;
        found = index;
    }
    return found;
}

// 車両を読む。Scene は保存されておらず、edge を持つ Scene から求める(決まらない車両は読み込まない)。
// 列車の先頭の位置(order が最小の車両)も、読み込めなかった車両を含めて決めて heads に入れる。
std::vector<rs::world::SceneEntity> RestoreCars(const nlohmann::json& world,
                                                const std::vector<LayoutSceneCache>& scenes,
                                                RslTrainHeads& heads, std::vector<std::string>& warnings) {
    std::vector<rs::world::SceneEntity> result;
    const nlohmann::json cars = world.value("cars", nlohmann::json::array());
    if (!cars.is_array()) {
        spdlog::warn("[rsl_layout_serialize] trains is not an array; skipping it.");
        return result;
    }

    std::map<rs::world::TrainId, int> headOrders;
    size_t droppedCount = 0;
    for (const nlohmann::json& item : cars) {
        rs::world::SceneEntity entity = rs::world::CarEntityFromJson(item);
        entity.category = rs::world::SceneEntityCategory::Car;
        const rs::world::TrackPlacement* placement = std::get_if<rs::world::TrackPlacement>(&entity.placement);
        const std::optional<size_t> sceneIndex =
            placement == nullptr ? std::nullopt : SceneIndexOfEdge(scenes, placement->position.edge);
        if (sceneIndex) entity.placementSceneIndex = *sceneIndex;
        if (!rs::world::GuidIsNil(entity.trainId)) {
            const auto order = headOrders.find(entity.trainId);
            if (order == headOrders.end() || entity.carOrder < order->second) {
                headOrders[entity.trainId] = entity.carOrder;
                heads[entity.trainId] = sceneIndex ? std::optional<RslTrainHead>(
                    RslTrainHead{ *sceneIndex, placement->position }) : std::nullopt;
            }
        }
        if (!sceneIndex) {
            spdlog::warn("[rsl_layout_serialize] car {} is on an edge that is in no scene (or several); skipping it.",
                         rs::world::GuidToHex(entity.guid));
            ++droppedCount;
            continue;
        }
        result.push_back(std::move(entity));
    }
    if (droppedCount > 0) {
        warnings.push_back("線路のedgeが特定のSceneに無い車両" + std::to_string(droppedCount) +
                           "両は、読み込みませんでした");
    }
    return result;
}

// 車両の編成id(初出の順、重複なし)。保存には配置済みの編成の一覧がなく、車両のtrainIdから求める。
std::vector<rs::world::TrainId> PlacedTrainIdsOf(const std::vector<rs::world::SceneEntity>& cars) {
    std::vector<rs::world::TrainId> trainIds;
    for (const rs::world::SceneEntity& car : cars) {
        if (rs::world::GuidIsNil(car.trainId)) continue;
        if (std::find(trainIds.begin(), trainIds.end(), car.trainId) == trainIds.end()) trainIds.push_back(car.trainId);
    }
    return trainIds;
}

void ReplaceSceneGuidsWithIndices(nlohmann::json& document) {
    const nlohmann::json& scenes = document.at("world").at("scenes");
    // 保存側(RslLayoutSerialize.cpp)は必ずscene/sceneA/sceneBへ存在するSceneのguidを
    // 文字列で書き出すため、非文字列・nil GUID・未知GUIDはいずれも手編集や破損以外では
    // 起こらない。index 0へ丸めず、他のScene GUID参照切れと同じく
    // 読込全体を失敗させる。
    const auto sceneIndex = [&scenes](const nlohmann::json& guid) -> size_t {
        if (!guid.is_string()) {
            throw RslInvalidLayoutError("The layout file references a scene using a non-string GUID.");
        }
        const rs::world::Guid requestedGuid = rs::world::GuidFromJson(guid);
        if (rs::world::GuidIsNil(requestedGuid)) {
            throw RslInvalidLayoutError("The layout file references a scene using a nil GUID.");
        }
        for (size_t index = 0; index < scenes.size(); ++index) {
            if (!scenes[index].is_object() || !scenes[index].contains("guid") ||
                !scenes[index]["guid"].is_string()) {
                continue;
            }
            if (rs::world::GuidFromJson(scenes[index]["guid"]) == requestedGuid) return index;
        }
        throw RslInvalidLayoutError("The layout file references a scene GUID that does not exist.");
    };
    nlohmann::json& world = document["world"];
    for (nlohmann::json& warp : world["warps"]) {
        warp["sceneA"] = sceneIndex(warp.value("sceneA", nlohmann::json{}));
        warp["sceneB"] = sceneIndex(warp.value("sceneB", nlohmann::json{}));
    }
}

rs::editor::TrackLayingSettings LayingSettingsFromJson(const nlohmann::json& json) {
    rs::editor::TrackLayingSettings settings;
    // 常時スナップは"angleSnapAlways"だけを見る(既定OFF)。旧キー"angleSnapEnabled"は読まない。
    settings.angleSnapEnabled = json.value("angleSnapAlways", settings.angleSnapEnabled);
    settings.angleSnapDeg = rs::editor::TrackLayingSettings::ClampAngleSnapDegrees(
        json.value("angleSnap", settings.angleSnapDeg));
    settings.trackCount = rs::editor::TrackLayingSettings::ClampTrackCount(
        json.value("trackCount", settings.trackCount));
    settings.trackSpacing = rs::editor::TrackLayingSettings::ClampTrackSpacing(
        json.value("trackSpacing", settings.trackSpacing));
    settings.transEnabled = json.value("transitionEnabled", settings.transEnabled);
    const std::string transShape = json.value("transitionShape", std::string{});
    settings.transShape = rs::world::TransitionShapeFromJson(
        transShape, rs::world::AlignmentSegment::TransitionShape::Clothoid);
    settings.transLen = rs::editor::TrackLayingSettings::ClampTransitionLength(
        json.value("transitionLength", settings.transLen));
    settings.defaultTrackPlugins = json.value("defaultPlugins", settings.defaultTrackPlugins);
    return settings;
}

}  // namespace

LayoutActionResult DecodeRsl(const std::string& pathUtf8, const RslPersistenceRefs& refs,
                        NormalizedLayout& normalized) {
    LayoutActionResult result;
    // 受け取った中身は使わず、既定値から作り直す(項目が欠けても前の値が残らない)。
    normalized = NormalizedLayout{};
    try {
        const std::filesystem::path path(rs::platform_text::Utf8ToWide(pathUtf8));
        std::ifstream stream(path, std::ios::binary);
        if (!stream.is_open()) {
            result = LayoutActionResult::Failure(LayoutActionStatus::FileNotFound, pathUtf8, "Could not open the layout file for reading.");
            return result;
        }
        nlohmann::json document = nlohmann::json::parse(stream);
        if (!document.contains("info") || !document.at("info").is_object() ||
            !document.at("info").contains("appVersion") || !document.at("info").at("appVersion").is_string()) {
            result = LayoutActionResult::Failure(LayoutActionStatus::InvalidFormat, pathUtf8,
                                                 "The layout file does not contain info.appVersion.");
            return result;
        }
        if (document.value("specVersion", 0) != 2) {
            result = LayoutActionResult::Failure(LayoutActionStatus::InvalidFormat, pathUtf8, "Unsupported layout specVersion.");
            return result;
        }
        ReplaceSceneGuidsWithIndices(document);
        if (document.contains("info") && document.at("info").is_object() &&
            document.at("info").contains("guid") && document.at("info")["guid"].is_string()) {
            result.guid = rs::world::GuidFromJson(document.at("info")["guid"]);
        }
        if (rs::world::GuidIsNil(result.guid)) result.guid = rs::world::GenerateGuid();
        // 登録済みのレール系プラグインと同じ綴りへ揃える(綴りが違うとレールが描かれない)。
        ResolveLayoutPluginReferences(document, refs.pluginRoots, [&refs](const std::string& folderUtf8) {
            return refs.selector.RegisteredTrackPluginFolder(folderUtf8);
        });
        const nlohmann::json& world = document.at("world");
        const nlohmann::json& scenes = world.at("scenes");
        if (!scenes.is_array() || scenes.empty()) {
            result = LayoutActionResult::Failure(LayoutActionStatus::InvalidFormat, pathUtf8,
                                                 "The layout file does not contain any scenes.");
            return result;
        }
        std::vector<LayoutSceneCache> loadedScenes;
        loadedScenes.reserve(scenes.size());
        for (const nlohmann::json& scene : scenes) {
            LayoutSceneCache cache;
            RestoreSceneCache(scene, cache);
            const std::vector<rs::world::PlacedIndividual> linePoles =
                BindSceneLinePoles(cache.manualScenery, cache.graph, cache.overlay);
            SanitizeOverlay(cache.overlay, cache.graph, CollectPoleIds(linePoles));
            loadedScenes.push_back(std::move(cache));
        }
        normalized.scenes = std::move(loadedScenes);
        RslTrainHeads trainHeads;
        normalized.cars = RestoreCars(world, normalized.scenes, trainHeads, normalized.warnings);
        normalized.placedTrainIds = PlacedTrainIdsOf(normalized.cars);
        // WarpLinkTableFromJsonが構築した新しいテーブルで丸ごと置き換えるため、
        // 事前のClear()は不要(代入演算子が前回セッション分を完全に上書きする)。
        normalized.warpLinks = rs::world::WarpLinkTableFromJson(
            world.value("warps", nlohmann::json::array()), normalized.scenes.size());
        if (world.contains("time")) {
            LayoutTimeFromJson(world.at("time"), normalized.gameClock, normalized.solarOverrides);
        }
        if (document.contains("editor") && document.at("editor").contains("layingSettings")) {
            normalized.layingSettings = LayingSettingsFromJson(document.at("editor").at("layingSettings"));
        }
        // キーが無いときは空のformationsとして扱う(保存時に空配列は省略されるため、
        // 「formationsキーが無い」と「編成0件」を区別しない)。
        normalized.formationDefsJson = world.value("formations", nlohmann::json::array());
        normalized.selectedFormationJson =
            document.contains("editor") && document.at("editor").is_object()
                ? document.at("editor").value("selectedFormation", nlohmann::json{})
                : nlohmann::json{};
        if (world.contains("trainTags")) {
            normalized.trainTags = rs::world::TrainTagRegistryFromJson(world.at("trainTags"));
        }
        if (world.contains("trainRunInfo")) {
            normalized.trainRunInfo = rs::world::TrainRunInfoRegistryFromJson(world.at("trainRunInfo"));
        }
        normalized.activeIndex = 0;
        const nlohmann::json activeSceneJson = world.value("activeScene", nlohmann::json{});
        if (activeSceneJson.is_string()) {
            const rs::world::Guid activeScene = rs::world::GuidFromJson(activeSceneJson);
            if (!rs::world::GuidIsNil(activeScene)) {
                bool activeSceneFound = false;
                for (size_t index = 0; index < normalized.scenes.size(); ++index) {
                    if (normalized.scenes[index].guid == activeScene) {
                        normalized.activeIndex = index;
                        activeSceneFound = true;
                        break;
                    }
                }
                if (!activeSceneFound) {
                    throw RslInvalidLayoutError(
                        "The layout file's world.activeScene references a scene GUID that does not exist.");
                }
            }
        }
        // 架線を保持しない呼び出し側(refs.importedWireSpans==nullptr)では復元結果を捨てる。
        if (document.contains("session")) {
            // 走行状態は線路より弱い情報なので、読めない分は警告して捨て、レイアウトは読む。
            RslRuntimeState runtimeState;
            size_t skippedTrains = 0;
            if (RslRuntimeStateFromJson(document.at("session"), trainHeads, runtimeState, &skippedTrains)) {
                normalized.runtime = std::move(runtimeState);
            } else {
                normalized.warnings.push_back("走行状態(session)の形式が不正なため、走行状態を読み込みませんでした");
            }
            if (skippedTrains > 0) {
                normalized.warnings.push_back("走行状態が線路と合わない列車" + std::to_string(skippedTrains) +
                                              "本は、走行状態を読み込みませんでした");
            }
        }
        result.nodeCount = normalized.scenes[normalized.activeIndex].graph.nodes.size();
        result.edgeCount = normalized.scenes[normalized.activeIndex].graph.edges.size();
        result.placementCount = CountPlacementItems(
            normalized.scenes[normalized.activeIndex].overlay,
            scenes[normalized.activeIndex].value("placements", nlohmann::json::object()));
        result.sceneCount = normalized.scenes.size();
    } catch (const RslInvalidLayoutError& exception) {
        // Scene GUID参照切れなど、読込全体を失敗させるべき不正。
        result = LayoutActionResult::Failure(LayoutActionStatus::InvalidFormat, pathUtf8, exception.what());
    } catch (const nlohmann::json::exception& exception) {
        // JSONとして壊れている・必要な要素が無い場合はここへ来る(at()のout_of_range含む)。
        result = LayoutActionResult::Failure(LayoutActionStatus::InvalidFormat, pathUtf8, exception.what());
    } catch (const std::exception& exception) {
        result = LayoutActionResult::Failure(LayoutActionStatus::Unexpected, exception.what(), exception.what());
    }
    return result;
}

}  // namespace rs::app

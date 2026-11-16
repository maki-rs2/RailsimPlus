#include "ScenePanelViewModel.h"

#include <algorithm>
#include <filesystem>
#include <format>
#include <unordered_map>

#include "../scene/SurfaceBoundsTracker.h"
#include "../../platform/text/AsciiCase.h"
#include "../../platform/text/Encoding.h"
#include "../../world/scene/SceneRegistry.h"

namespace rs::app {

std::vector<SceneContentCounts> CountSceneContents(const LayoutSession& session,
                                                  const rs::world::SceneRegistry& registry) {
    // シーンが空のセッションは、ライブの1シーンだけがある扱いにする。
    std::vector<SceneContentCounts> counts(std::max<size_t>(session.scenes.size(), 1));
    for (const rs::world::WarpLink& link : session.warpLinks.Links()) {
        if (link.a.sceneIndex < counts.size()) ++counts[link.a.sceneIndex].warps;
        if (link.b.sceneIndex != link.a.sceneIndex && link.b.sceneIndex < counts.size()) {
            ++counts[link.b.sceneIndex].warps;
        }
    }
    // 車両が複数のシーンにまたがる編成も、先頭車の所属で1編成として数える。
    std::unordered_map<rs::world::TrainId, const rs::world::SceneEntity*, rs::world::GuidHash> leads;
    for (const rs::world::SceneEntity& entity : registry.Entities()) {
        if (entity.category != rs::world::SceneEntityCategory::Car || !rs::world::IsUserPlacement(entity) ||
            rs::world::GuidIsNil(entity.trainId)) continue;
        const auto [entry, inserted] = leads.emplace(entity.trainId, &entity);
        if (!inserted && entity.carOrder < entry->second->carOrder) entry->second = &entity;
    }
    for (const auto& [id, lead] : leads) {
        if (lead->placementSceneIndex < counts.size()) ++counts[lead->placementSceneIndex].trains;
    }
    return counts;
}

std::string SurfaceNameOfFolder(const std::string& folderUtf8) {
    if (!IsSurfacePluginFolder(folderUtf8)) return {};
    return rs::platform_text::WideToUtf8(
        std::filesystem::path(rs::platform_text::Utf8ToWide(folderUtf8)).filename().wstring());
}

std::string SurfaceNameOfEntities(const std::vector<rs::world::SceneEntity>& entities) {
    for (const rs::world::SceneEntity& entity : entities) {
        std::string name = SurfaceNameOfFolder(entity.folderUtf8);
        if (!name.empty()) return name;
    }
    return {};
}

std::vector<workspace::SceneEnvOption> BuildEnvOptions(const std::string& currentEnv,
                                                       const std::vector<SceneChoice>& envs, size_t& selected) {
    std::vector<workspace::SceneEnvOption> options;
    const auto matches = [&currentEnv](const SceneChoice& choice) {
        return rs::platform_text::EqualsIgnoreAsciiCase(choice.name, currentEnv);
    };
    const std::vector<SceneChoice>::const_iterator found = std::find_if(envs.begin(), envs.end(), matches);
    if (!currentEnv.empty() && found == envs.end()) {
        options.push_back({currentEnv + "(選べない環境)", currentEnv, true});
    }
    const size_t defaultIndex = options.size();
    options.push_back({"デフォルトの空", std::string(), false});
    for (const SceneChoice& choice : envs) options.push_back({choice.name, choice.name, false});
    if (currentEnv.empty()) selected = defaultIndex;
    else if (found == envs.end()) selected = 0;
    else selected = defaultIndex + 1 + static_cast<size_t>(found - envs.begin());
    return options;
}

std::string BuildSceneDeleteMessage(const Localization& localization, const std::string& sceneName, int warps,
                                    int trains) {
    std::string message = std::vformat(localization.Text("scene.delete.confirm"), std::make_format_args(sceneName));
    if (warps > 0 || trains > 0) {
        message += std::vformat(localization.Text("scene.delete.contents"), std::make_format_args(warps, trains));
    }
    return message;
}

void ScenePanelSource::Rescan(const std::vector<std::string>& rootCandidatesUtf8) {
    choices_ = ScanSceneChoices(rootCandidatesUtf8);
    scanned_ = true;
}

const std::vector<std::string>& ScenePanelSource::SurfaceNames(const LayoutSession& session,
                                                               const rs::world::SceneRegistry& registry) {
    Signature signature;
    signature.active = session.activeIndex;
    signature.liveCount = registry.Entities().size();
    for (const LayoutSceneCache& scene : session.scenes) {
        signature.scenes.emplace_back(scene.manualScenery.data(), scene.manualScenery.size());
    }
    if (hasNames_ && signature == signature_) return surfaceNames_;
    signature_ = std::move(signature);
    hasNames_ = true;
    surfaceNames_.assign(std::max<size_t>(session.scenes.size(), 1), std::string());
    for (size_t index = 0; index < session.scenes.size(); ++index) {
        if (index != session.activeIndex) surfaceNames_[index] = SurfaceNameOfEntities(session.scenes[index].manualScenery);
    }
    // 今のシーン(シーンが空のセッションではライブの1つだけ)は、キャッシュが古いことがあるのでライブの配置物から求める。
    if (session.activeIndex < surfaceNames_.size()) {
        for (const rs::world::SceneEntity& entity : registry.Entities()) {
            if (!rs::world::IsUserPlacement(entity)) continue;
            surfaceNames_[session.activeIndex] = SurfaceNameOfFolder(entity.folderUtf8);
            if (!surfaceNames_[session.activeIndex].empty()) break;
        }
    }
    return surfaceNames_;
}

bool FillScenePanelFromSession(workspace::ScenePanelPresentation& panel, const LayoutSession& session,
                               const rs::world::SceneRegistry& registry, ScenePanelSource& source) {
    const std::vector<SceneContentCounts> counts = CountSceneContents(session, registry);
    const std::vector<std::string>& surfaceNames = source.SurfaceNames(session, registry);
    std::vector<workspace::ScenePanelRow> rows;
    rows.reserve(counts.size());
    for (size_t index = 0; index < counts.size(); ++index) {
        workspace::ScenePanelRow row;
        row.name_ = index < session.scenes.size() ? session.scenes[index].name : MakeNewSceneName(session.scenes);
        row.warpCount_ = counts[index].warps;
        row.trainCount_ = counts[index].trains;
        row.current_ = index == session.activeIndex;
        if (index < panel.rows_.size()) {
            row.editing_ = panel.rows_[index].editing_;
            row.draft_ = panel.rows_[index].draft_;
        }
        size_t selected = 0;
        row.envOptions_ = BuildEnvOptions(index < session.scenes.size() ? session.scenes[index].envPlugin : std::string(),
                                          source.Choices().envs, selected);
        row.env_ = std::to_string(selected);
        row.surface_ = index < surfaceNames.size() ? surfaceNames[index] : std::string();
        rows.push_back(std::move(row));
    }
    std::vector<workspace::SceneSurfaceChoice> surfaces;
    for (const SceneChoice& choice : source.Choices().surfaces) surfaces.push_back({choice.name, choice.folderUtf8});
    if (rows == panel.rows_ && surfaces == panel.surfaceChoices_) return false;
    panel.rows_ = std::move(rows);
    panel.surfaceChoices_ = std::move(surfaces);
    panel.RefreshDerived();
    return true;
}

} // namespace rs::app

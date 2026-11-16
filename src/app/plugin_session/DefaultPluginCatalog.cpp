#include "DefaultPluginCatalog.h"

#include <filesystem>
#include <set>
#include <utility>

#include <spdlog/spdlog.h>

#include "PluginRootResolver.h"
#include "../plugin_browser/PluginTreeConfigSource.h"
#include "Rs2SceneApplier.h"
#include "../../platform/text/Encoding.h"
#include "../../plugins/layout/Rs2Schema.h"
#include "../../world/scene/SceneRegistry.h"
#include "../settings/UserSettings.h"

namespace rs::app {
namespace {

const DefaultPluginTraits* FindDefaultPluginKind(rs::legacy_pi::PluginType type) {
    for (const DefaultPluginTraits& row : kDefaultPluginKinds) {
        if (row.pluginType == type) return &row;
    }
    return nullptr;
}

// ルートからの相対パス("Surface/Default"の形)。ユーザー設定、表の既定名の順に探す。
std::string DefaultRelativePath(const DefaultPluginTraits& row, const UserSettingsData& settings) {
    const std::map<std::string, std::string>::const_iterator found = settings.defaultPlugins.find(row.folderName);
    if (found != settings.defaultPlugins.end() && !found->second.empty()) return found->second;
    if (row.defaultName[0] == '\0') return {};
    return std::string(row.folderName) + "/" + row.defaultName;
}

}  // namespace

std::optional<std::string> ResolveDefaultPlugin(rs::legacy_pi::PluginType type,
                                                const UserSettingsData& settings,
                                                const std::vector<std::string>& rootsUtf8) {
    const DefaultPluginTraits* row = FindDefaultPluginKind(type);
    if (row == nullptr) return std::nullopt;
    const std::string relativePath = DefaultRelativePath(*row, settings);
    if (relativePath.empty()) return std::nullopt;
    std::set<std::string> warnedConflicts;
    const std::string folder = ResolveAgainstPluginRoots(rootsUtf8, relativePath, warnedConflicts);
    if (!std::filesystem::exists(std::filesystem::path(rs::platform_text::Utf8ToWide(folder)))) {
        return std::nullopt;
    }
    return folder;
}

std::vector<std::string> ResolveConfigTrackPluginFolders(const std::vector<std::string>& rootsUtf8) {
    std::vector<std::string> folders;
    const std::map<std::string, std::string> selected = plugin_browser::LoadConfigSelectedPlugins(rootsUtf8);
    std::set<std::string> warnedConflicts;
    for (const DefaultPluginTraits& row : kDefaultPluginKinds) {
        if (!row.trackPlugin) continue;
        const std::map<std::string, std::string>::const_iterator found = selected.find(row.folderName);
        if (found == selected.end()) continue;
        const std::string folder =
            ResolveAgainstPluginRoots(rootsUtf8, std::string(row.folderName) + "/" + found->second, warnedConflicts);
        if (std::filesystem::exists(std::filesystem::path(rs::platform_text::Utf8ToWide(folder)))) {
            folders.push_back(folder);
        }
    }
    return folders;
}

std::vector<rs::world::SceneEntity> MakeSurfaceEntitiesFromFolder(const std::string& folderUtf8,
                                                                  const std::string& kindName,
                                                                  const std::string& sceneKeyUtf8) {
    // 解決済みのフォルダを持たせるため、ImportPlacementsには空のルートを渡して形を作り、
    // フォルダだけここで差し替える(原点の姿勢への変換は.rs2取り込みと同じ経路を通す)。
    rs::legacy_layout::Rs2Scene scene;
    const std::string pluginName = rs::platform_text::WideToUtf8(
        std::filesystem::path(rs::platform_text::Utf8ToWide(folderUtf8)).filename().wstring());
    rs::legacy_layout::Rs2PlacedInstance instance = rs::legacy_layout::MakeSurfaceInstance(pluginName);
    instance.kind = kindName;
    scene.placements.push_back(std::move(instance));
    std::vector<std::string> warnings;
    std::vector<rs::legacy_layout::ImportedPlacement> placements =
        rs::legacy_layout::ImportPlacements(scene, {}, warnings);
    for (rs::legacy_layout::ImportedPlacement& placement : placements) placement.pluginFolderUtf8 = folderUtf8;
    return SceneEntitiesFromImportedPlacements(placements, sceneKeyUtf8);
}

std::size_t EnsureDefaultPlacements(rs::world::SceneRegistry& sceneRegistry,
                                    const UserSettingsData& settings,
                                    const std::vector<std::string>& rootsUtf8) {
    std::size_t placedCount = 0;
    for (const DefaultPluginTraits& row : kDefaultPluginKinds) {
        if (!row.autoPlace) continue;
        const std::optional<std::string> folder = ResolveDefaultPlugin(row.pluginType, settings, rootsUtf8);
        if (!folder) {
            spdlog::warn("[default_plugin] {}の既定プラグインが見つからないため、配置しません。", row.folderName);
            continue;
        }
        for (rs::world::SceneEntity& entity : MakeSurfaceEntitiesFromFolder(*folder, row.folderName, "default")) {
            sceneRegistry.AddEntity(std::move(entity));
            ++placedCount;
        }
    }
    return placedCount;
}

}  // namespace rs::app

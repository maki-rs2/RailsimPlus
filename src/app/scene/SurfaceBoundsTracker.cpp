#include "SurfaceBoundsTracker.h"

#include <filesystem>

#include "../../platform/text/Encoding.h"
#include "../../plugins/load/PluginDefinitionLookup.h"

namespace rs::app {

bool IsSurfacePluginFolder(const std::string& folderUtf8) {
    if (folderUtf8.empty()) return false;
    const std::filesystem::path folder(rs::platform_text::Utf8ToWide(folderUtf8));
    return folder.parent_path().filename() == L"Surface";
}

const std::optional<rs::world::SurfaceBounds>& SurfaceBoundsTracker::Update(
    const rs::world::SceneRegistry& registry) {
    if (registry.Revision() == lastRevision_) return bounds_;
    lastRevision_ = registry.Revision();
    bounds_.reset();
    for (const rs::world::SceneEntity& entity : registry.Entities()) {
        if (entity.category != rs::world::SceneEntityCategory::Scenery) continue;
        if (!IsSurfacePluginFolder(entity.folderUtf8)) continue;
        const rs::legacy_pi::ParsedDefinitionResult definition =
            rs::legacy_plugin::LoadPluginDefinition(entity.folderUtf8);
        if (!definition.ok || !definition.def.hasSurfaceInfo) continue;
        bounds_ = rs::world::SurfaceBounds{definition.def.surfaceSizeX, definition.def.surfaceSizeZ};
        break;
    }
    return bounds_;
}

}  // namespace rs::app

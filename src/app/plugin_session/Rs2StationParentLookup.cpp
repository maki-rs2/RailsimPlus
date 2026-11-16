#include "Rs2StationParentLookup.h"

#include <algorithm>
#include <iterator>
#include <memory>
#include <unordered_map>

#include "../../plugins/layout/Rs2Import.h"
#include "../../plugins/load/PluginDefinitionLookup.h"

namespace rs::app {

namespace {

using rs::legacy_layout::StationParentInfo;
using rs::legacy_layout::StationPlatformParent;

std::optional<StationParentInfo> BuildStationParentInfo(const std::string& folderUtf8) {
    const rs::legacy_pi::ParsedDefinitionResult parsed = rs::legacy_plugin::LoadPluginDefinition(folderUtf8);
    if (!parsed.ok) return std::nullopt;

    StationParentInfo info;
    bool anyParent = false;
    for (const rs::legacy_pi::PlatformDef& platform : parsed.def.platforms) {
        StationPlatformParent parent;
        parent.partIndex = platform.parentPartIndex;
        parent.undefined = platform.parentUndefined;
        anyParent = anyParent || parent.partIndex >= 0 || parent.undefined;
        info.platforms.push_back(parent);
    }
    if (!anyParent) return std::nullopt;

    for (const rs::legacy_pi::ModelPart& part : parsed.def.parts) {
        rs::core::Mat4 matrix;
        std::copy(std::begin(part.worldMat), std::end(part.worldMat), matrix.m);
        info.partRestMatrices.push_back(matrix);
    }
    return info;
}

} // namespace

rs::legacy_layout::StationParentLookup MakeStationParentLookup(const std::string& installRootUtf8) {
    using Memo = std::unordered_map<std::string, std::optional<StationParentInfo>>;
    const std::shared_ptr<Memo> memo = std::make_shared<Memo>();
    return [installRootUtf8, memo](const rs::legacy_layout::Rs2PlacedInstance& station)
               -> std::optional<StationParentInfo> {
        const std::string folder =
            rs::legacy_layout::ResolveTrackPluginFolder(installRootUtf8, "Station", station.pluginName);
        if (folder.empty()) return std::nullopt;
        const Memo::const_iterator cached = memo->find(folder);
        if (cached != memo->end()) return cached->second;
        return memo->emplace(folder, BuildStationParentInfo(folder)).first->second;
    };
}

} // namespace rs::app

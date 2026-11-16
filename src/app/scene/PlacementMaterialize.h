//
// PlacementMaterialize.h - 自動配置結果からの実体化スナップショット生成
//
#pragma once

#include "../../world/scene/MaterializeSnapshot.h"
#include "../plugin_session/PluginSelector.h"

namespace rs::app {

rs::world::MaterializeSnapshot BuildMaterializeSnapshot(
    const rs::world::TrackGraph& graph, const std::vector<TrackPluginEntry>& entries,
    const rs::world::Guid& edge, const std::string& folderUtf8);

std::vector<rs::world::MaterializeSnapshot> BuildMaterializeSnapshotsForEdit(
    const rs::world::TrackGraph& graph, const std::vector<TrackPluginEntry>& entries,
    const std::vector<rs::world::Guid>& edges, const std::string& folderUtf8);

} // namespace rs::app

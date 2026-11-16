//
// PlacementRescaleCommand.cpp - PlacementRescaleCommand.h の実装
//
#include "PlacementRescaleCommand.h"
#include "Editor.h"

#include <unordered_map>
#include <utility>

#include "../../world/alignment/TrackTraversal.h"
#include "../../world/scene/MaterializeSnapshot.h"

namespace rs::editor {

using rs::world::Guid;
using rs::world::GuidHash;
using rs::world::ManualPlacementOverlay;

namespace {

bool OverlayHasPlacements(const ManualPlacementOverlay& overlay) {
    return !overlay.poles.empty() || !overlay.piers.empty() || !overlay.runAnchors.empty();
}

} // namespace

RescalePlacementCommand::RescalePlacementCommand(ManualPlacementOverlay& overlay,
                                                 std::unique_ptr<ICommand> inner)
    : overlay_(overlay), inner_(std::move(inner)) {}

void RescalePlacementCommand::Apply(rs::world::TrackGraph& graph) {
    savedOverlay_ = overlay_;
    if (!OverlayHasPlacements(overlay_)) {
        inner_->Apply(graph);
        return;
    }
    std::unordered_map<Guid, double, GuidHash> before;
    before.reserve(graph.edges.size());
    for (const std::pair<const Guid, rs::world::TrackEdge>& entry : graph.edges)
        before.emplace(entry.first, rs::world::EdgeLength(entry.second));

    inner_->Apply(graph);

    for (const std::pair<const Guid, rs::world::TrackEdge>& entry : graph.edges) {
        const std::unordered_map<Guid, double, GuidHash>::const_iterator it = before.find(entry.first);
        if (it == before.end()) continue;
        rs::world::RescaleMaterializedEdgeItems(overlay_, entry.first, it->second,
                                                rs::world::EdgeLength(entry.second));
    }
}

void RescalePlacementCommand::Revert(rs::world::TrackGraph& graph) {
    inner_->Revert(graph);
    overlay_ = savedOverlay_;
}

bool ExecuteWithPlacementRescale(Editor& editor,
                                 ManualPlacementOverlay* overlay, std::unique_ptr<ICommand> cmd) {
    if (overlay != nullptr)
        cmd = std::make_unique<RescalePlacementCommand>(*overlay, std::move(cmd));
    return editor.ExecuteCommand(std::move(cmd));
}

} // namespace rs::editor

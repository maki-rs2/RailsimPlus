//
// PlacementOverlayCommand.cpp - PlacementOverlayCommand.h の実装
//
#include "PlacementOverlayCommand.h"

#include <algorithm>
#include <utility>

namespace rs::editor {

using rs::world::Guid;
using rs::world::ManualPlacementOverlay;

ClearPlacementEditsCommand::ClearPlacementEditsCommand(ManualPlacementOverlay& overlay,
                                                       std::vector<Guid> edges)
    : overlay_(overlay), edges_(std::move(edges)) {}

void ClearPlacementEditsCommand::Apply(rs::world::TrackGraph& /*graph*/) {
    savedOverlay_ = overlay_;
    if (edges_.empty()) return;

    auto targeted = [this](const Guid& edge) {
        return std::find(edges_.begin(), edges_.end(), edge) != edges_.end();
    };

    std::vector<rs::world::Guid> removedPoleIds;
    for (const rs::world::PlacedIndividual& pole : overlay_.poles) {
        if (targeted(pole.edge)) removedPoleIds.push_back(pole.id);
    }
    overlay_.poles.erase(std::remove_if(overlay_.poles.begin(), overlay_.poles.end(),
        [&targeted](const rs::world::PlacedIndividual& item) { return targeted(item.edge); }), overlay_.poles.end());
    overlay_.piers.erase(std::remove_if(overlay_.piers.begin(), overlay_.piers.end(),
        [&targeted](const rs::world::PlacedIndividual& item) { return targeted(item.edge); }), overlay_.piers.end());
    overlay_.lines.erase(std::remove_if(overlay_.lines.begin(), overlay_.lines.end(),
        [&removedPoleIds](const rs::world::WireSpan& line) {
            return std::find(removedPoleIds.begin(), removedPoleIds.end(), line.poleA) != removedPoleIds.end() ||
                   std::find(removedPoleIds.begin(), removedPoleIds.end(), line.poleB) != removedPoleIds.end();
        }), overlay_.lines.end());
    overlay_.runAnchors.erase(std::remove_if(overlay_.runAnchors.begin(), overlay_.runAnchors.end(),
        [&targeted](const rs::world::RunAnchor& section) { return targeted(section.edge); }), overlay_.runAnchors.end());
}

void ClearPlacementEditsCommand::Revert(rs::world::TrackGraph& /*graph*/) {
    overlay_ = savedOverlay_;
}

} // namespace rs::editor

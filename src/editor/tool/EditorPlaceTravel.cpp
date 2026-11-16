#include "Editor.h"

#include "../../world/alignment/TravelDirectionFollow.h"

namespace rs::editor {

int Editor::ResolvePlaceTravelDirection(const rs::world::Guid& edge, bool reverse,
                                        const rs::world::WarpLinkTable* warpLinks,
                                        std::size_t sceneIndex) const {
    int base = +1;
    if (lastPlacedTravel.has_value()) {
        const std::optional<int> followed = rs::world::FollowTravelDirection(
            graph, lastPlacedTravel->edge, lastPlacedTravel->direction, edge, warpLinks, sceneIndex);
        if (followed.has_value()) base = *followed;
    }
    return reverse ? -base : base;
}

} // namespace rs::editor

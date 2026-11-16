#pragma once

#include <cstddef>

#include "TrainOccupiedPath.h"

namespace rs::sim {

struct LeadingEnd {
    size_t sceneIndex = 0;
    rs::world::TrackPosition position;
};

LeadingEnd ResolveLeadingEnd(const GraphForScene& graphForScene,
                             const rs::world::WarpLinkTable& warpTable, size_t headScene,
                             const rs::world::TrackPosition& head, bool travelReversed,
                             const TrainOccupiedPath& occupiedPath, double keepLengthM);

} // namespace rs::sim

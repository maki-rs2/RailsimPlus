#include "TrainLeadingEnd.h"

#include <algorithm>

#include "../world/alignment/WarpTraversal.h"

namespace rs::sim {

LeadingEnd ResolveLeadingEnd(const GraphForScene& graphForScene,
                             const rs::world::WarpLinkTable& warpTable, size_t headScene,
                             const rs::world::TrackPosition& head, bool travelReversed,
                             const TrainOccupiedPath& occupiedPath, double keepLengthM) {
    LeadingEnd leadingEnd{headScene, head};
    if (!travelReversed) return leadingEnd;

    size_t tailScene = headScene;
    rs::world::TrackPosition tailPosition;
    const double lengthM = std::max(0.0, keepLengthM);
    if (ChaseBackwardOnPath(occupiedPath, graphForScene, lengthM, tailScene, tailPosition)) {
        leadingEnd.sceneIndex = tailScene;
        leadingEnd.position = tailPosition;
        leadingEnd.position.direction = -leadingEnd.position.direction;
        return leadingEnd;
    }

    rs::world::TrackPosition fallbackStart = head;
    fallbackStart.direction = -fallbackStart.direction;
    const rs::world::WarpAdvanceResult fallback = rs::world::AdvanceAcrossScenes(
        graphForScene, warpTable, headScene, fallbackStart, lengthM);
    leadingEnd.sceneIndex = fallback.sceneIndex;
    leadingEnd.position = fallback.pos;
    // 後方走査の進行向き(-head.direction側)がそのまま逆走時の進行方向になる。
    // 途中でedgeの向きが変わりうるため、head.directionの符号反転では代用できない。
    leadingEnd.position.direction = fallback.pos.direction;
    return leadingEnd;
}

} // namespace rs::sim

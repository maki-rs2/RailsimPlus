#include "PointHandLinks.h"

#include "TrackGraph.h"
#include "TrackTraversal.h"

namespace rs::world {

std::vector<PointSideLinks> BuildPointSideLinks(const TrackGraph& graph, const TrackNode& node) {
    constexpr int kLeftHand = 0;
    constexpr int kRightHand = 1;
    std::vector<PointSideLinks> sides;
    for (int side = 0; side < 2; ++side) {
        if (!node.IsBranching(side)) continue;
        PointSideLinks row;
        row.side = side;
        row.leftLinkIndex = node.LinkIndexOnSide(side, ResolveHandToOrdinal(graph, node, side, kLeftHand));
        row.rightLinkIndex = node.LinkIndexOnSide(side, ResolveHandToOrdinal(graph, node, side, kRightHand));
        row.leftActive = node.IsLinkActive(row.leftLinkIndex);
        row.rightActive = node.IsLinkActive(row.rightLinkIndex);
        sides.push_back(row);
    }
    return sides;
}

} // namespace rs::world

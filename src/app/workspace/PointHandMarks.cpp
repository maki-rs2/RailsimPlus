#include "PointHandMarks.h"

#include <algorithm>
#include <optional>

#include "../../world/alignment/PointHandLinks.h"
#include "../../world/alignment/TrackGraph.h"
#include "../../world/alignment/TrackTraversal.h"

namespace rs::app::workspace {
namespace {

// link の線上の、分岐の端から distanceM の点。端が edge の終点側なら終点から数える。
std::optional<rs::core::Vec3> PositionOnLink(const rs::world::TrackGraph& graph, const rs::world::TrackNode::Link& link,
                                             double distanceM) {
    const auto edge = graph.edges.find(link.edge);
    if (edge == graph.edges.end()) return std::nullopt;
    const double length = rs::world::EdgeLength(edge->second);
    const double along = std::min(distanceM, length * 0.5);
    return rs::world::EvaluateAt(edge->second, link.end == 0 ? along : length - along, +1).pos;
}

} // namespace

std::vector<PointHandMark> BuildPointHandMarks(const rs::world::TrackGraph& graph, const rs::world::TrackNode& node,
                                               double distanceM) {
    const std::vector<rs::world::PointSideLinks> sides = rs::world::BuildPointSideLinks(graph, node);
    std::vector<PointHandMark> marks;
    for (std::size_t row = 0; row < sides.size(); ++row) {
        // 番号は左タブの開通先の行(開通先1・開通先2)と同じ。面が1つだけのときは付けない。
        const std::string prefix = sides.size() == 1 ? std::string() : std::to_string(row + 1) + " ";
        for (const bool left : {true, false}) {
            const int linkIndex = left ? sides[row].leftLinkIndex : sides[row].rightLinkIndex;
            if (linkIndex < 0 || linkIndex >= static_cast<int>(node.links.size())) continue;
            const std::optional<rs::core::Vec3> position =
                PositionOnLink(graph, node.links[static_cast<std::size_t>(linkIndex)], distanceM);
            if (!position.has_value()) continue;
            marks.push_back({linkIndex, prefix + (left ? "左" : "右"), left ? sides[row].leftActive : sides[row].rightActive,
                             *position});
        }
    }
    return marks;
}

} // namespace rs::app::workspace

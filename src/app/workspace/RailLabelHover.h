#pragma once

#include <optional>
#include <vector>

#include "../../world/alignment/Guid.h"

namespace rs::world {
struct TrackGraph;
struct Block;
}

namespace rs::app::workspace {

// sはedgeの始点からの弧長[m]。カメラが動いても同じ軌道上の点を指す。
struct RailLabelAnchor {
    rs::world::Guid edge = rs::world::NilGuid();
    double s = 0.0;
    bool operator==(const RailLabelAnchor&) const = default;
};

struct RailLabelHoverInput {
    std::optional<RailLabelAnchor> hoveredRail;
    bool pointerOverLabel = false;
    rs::world::Guid selectedBlock = rs::world::NilGuid();
    rs::world::Guid editingLabel = rs::world::NilGuid();
    double deltaSeconds = 0.0;
};

struct RailLabel {
    rs::world::Guid block = rs::world::NilGuid();
    RailLabelAnchor anchor;
};

// 閉塞の中点(閉塞のレールを並びの順に全長でたどって、半分の長さになる点)。レールが無い閉塞はnullopt。
std::optional<RailLabelAnchor> BlockMidpointAnchor(const rs::world::TrackGraph& graph, const rs::world::Block& block);

class RailLabelHover {
public:
    explicit RailLabelHover(double graceSeconds);

    // 戻り値はgraph.blocksの順。重なる閉塞も省略せず、同じアンカーで縦に並べるための順を保つ。
    std::vector<RailLabel> Update(const rs::world::TrackGraph& graph, const RailLabelHoverInput& input);

private:
    double graceSeconds_;
    double remainingSeconds_ = 0.0;
    rs::world::Guid hoveredEdge_ = rs::world::NilGuid();
    std::optional<RailLabelAnchor> hoverAnchor_;
    rs::world::Guid selectedBlock_ = rs::world::NilGuid();
    std::optional<RailLabelAnchor> selectedAnchor_;
    std::vector<RailLabel> labels_;
};

} // namespace rs::app::workspace

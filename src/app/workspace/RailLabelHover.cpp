#include "RailLabelHover.h"

#include <algorithm>

#include "../../world/alignment/TrackTraversal.h"

namespace rs::app::workspace {
namespace {

bool ContainsEdge(const rs::world::Block& block, const rs::world::Guid& edge) {
    return std::find(block.edges.begin(), block.edges.end(), edge) != block.edges.end();
}

// 閉塞の先頭レールが無い場合は、画面に置けるアンカーも無い。
std::optional<RailLabelAnchor> ResolveAnchor(const rs::world::TrackGraph& graph, const rs::world::Block& block,
                                            const std::optional<RailLabelAnchor>& preferred) {
    if (preferred.has_value() && ContainsEdge(block, preferred->edge) && graph.edges.contains(preferred->edge)) {
        return preferred;
    }
    if (block.edges.empty()) return std::nullopt;
    const std::unordered_map<rs::world::Guid, rs::world::TrackEdge, rs::world::GuidHash>::const_iterator found =
        graph.edges.find(block.edges.front());
    if (found == graph.edges.end()) return std::nullopt;
    return RailLabelAnchor{found->first, rs::world::EdgeLength(found->second) * 0.5};
}

} // namespace

std::optional<RailLabelAnchor> BlockMidpointAnchor(const rs::world::TrackGraph& graph, const rs::world::Block& block) {
    double total = 0.0;
    for (const rs::world::Guid& edgeId : block.edges) {
        const auto found = graph.edges.find(edgeId);
        if (found != graph.edges.end()) total += rs::world::EdgeLength(found->second);
    }
    double remaining = total * 0.5;
    std::optional<RailLabelAnchor> last;
    for (const rs::world::Guid& edgeId : block.edges) {
        const auto found = graph.edges.find(edgeId);
        if (found == graph.edges.end()) continue;
        const double length = rs::world::EdgeLength(found->second);
        last = RailLabelAnchor{edgeId, std::min(remaining, length)};
        if (remaining <= length) break;
        remaining -= length;
    }
    return last;
}

RailLabelHover::RailLabelHover(double graceSeconds) : graceSeconds_(graceSeconds) {}

std::vector<RailLabel> RailLabelHover::Update(const rs::world::TrackGraph& graph, const RailLabelHoverInput& input) {
    // ラベルの操作中は、UIによって3Dホバーが取れなくてもアンカーと猶予を保つ。
    const bool holding = input.pointerOverLabel || !rs::world::GuidIsNil(input.editingLabel);
    if (input.hoveredRail.has_value() && graph.edges.contains(input.hoveredRail->edge)) {
        if (hoveredEdge_ != input.hoveredRail->edge) hoverAnchor_ = input.hoveredRail;
        hoveredEdge_ = input.hoveredRail->edge;
        remainingSeconds_ = graceSeconds_;
    } else if (!holding) {
        hoveredEdge_ = rs::world::NilGuid();
        remainingSeconds_ = std::max(0.0, remainingSeconds_ - std::max(0.0, input.deltaSeconds));
        if (remainingSeconds_ <= 0.0) hoverAnchor_.reset();
    }

    if (selectedBlock_ != input.selectedBlock) {
        selectedBlock_ = input.selectedBlock;
        selectedAnchor_ = hoverAnchor_;
        for (const RailLabel& label : labels_) {
            if (label.block == selectedBlock_) selectedAnchor_ = label.anchor;
        }
    }

    const bool hasHoverAnchor = hoverAnchor_.has_value() && graph.edges.contains(hoverAnchor_->edge);
    std::vector<RailLabel> labels;
    for (const rs::world::Block& block : graph.blocks) {
        std::optional<RailLabelAnchor> anchor;
        if (hasHoverAnchor && ContainsEdge(block, hoverAnchor_->edge)) {
            anchor = hoverAnchor_;
        }
        // 選択のトグルでdata-forの添字がずれると、同じラベルへのdblclickが届かない。
        const bool editing = block.guid == input.editingLabel;
        if (input.pointerOverLabel || editing) {
            std::optional<RailLabelAnchor> heldAnchor = anchor;
            for (const RailLabel& label : labels_) {
                if (label.block == block.guid) heldAnchor = label.anchor;
            }
            if (editing || heldAnchor.has_value()) anchor = ResolveAnchor(graph, block, heldAnchor);
        }
        if (block.guid == selectedBlock_) {
            selectedAnchor_ = ResolveAnchor(graph, block, selectedAnchor_);
            anchor = selectedAnchor_;
        }
        if (anchor.has_value()) labels.push_back({block.guid, *anchor});
    }
    labels_ = labels;
    return labels;
}

} // namespace rs::app::workspace

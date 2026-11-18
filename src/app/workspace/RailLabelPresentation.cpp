#include "RailLabelPresentation.h"

#include <algorithm>
#include "../../world/alignment/TrackGraph.h"

namespace rs::app::workspace {

bool CanCommitBlockName(const rs::world::Block* block, std::string_view name) {
    return block != nullptr && !name.empty() && block->name != name;
}

std::string BlockDisplayName(const rs::world::Block& block, std::size_t index) {
    return block.name.empty() ? "閉塞 " + std::to_string(index + 1) : block.name;
}

std::vector<RailLabelItem> BuildRailLabelItems(const rs::world::TrackGraph& graph,
                                              std::span<const RailLabel> labels,
                                              const rs::world::Guid& selectedBlock) {
    std::vector<RailLabelItem> items;
    for (std::size_t index = 0; index < graph.blocks.size(); ++index) {
        const rs::world::Block& block = graph.blocks[index];
        const auto label = std::find_if(labels.begin(), labels.end(), [&block](const RailLabel& candidate) {
            return candidate.block == block.guid;
        });
        if (label == labels.end()) continue;
        std::size_t verticalIndex = 0;
        for (const RailLabelItem& previous : items) {
            if (previous.anchor == label->anchor) ++verticalIndex;
        }
        items.push_back({block.guid, BlockDisplayName(block, index),
                         block.guid == selectedBlock, label->anchor, verticalIndex});
    }
    return items;
}

std::vector<RailLabelItem> BuildRailLabelCandidateItems(const rs::world::TrackGraph& graph,
    const RailLabelAnchor& anchor, std::span<const RailLabelCandidate> candidates) {
    std::vector<RailLabel> labels;
    for (const RailLabelCandidate& candidate : candidates) labels.push_back({candidate.block, anchor});
    std::vector<RailLabelItem> items = BuildRailLabelItems(graph, labels, rs::world::NilGuid());
    for (RailLabelItem& item : items) {
        const auto candidate = std::find_if(candidates.begin(), candidates.end(),
            [&item](const RailLabelCandidate& value) { return value.block == item.guid; });
        item.candidate = true;
        item.disabled = !candidate->blockedMessage.empty();
        item.title = candidate->blockedMessage;
    }
    return items;
}

RailLabelStack BuildRailLabelStack(std::span<const RailLabelSize> sizes, double gap) {
    RailLabelStack stack;
    for (const RailLabelSize& size : sizes) {
        if (!stack.tops.empty()) stack.size.height += gap;
        stack.tops.push_back(stack.size.height);
        stack.size.height += size.height;
        stack.size.width = std::max(stack.size.width, size.width);
    }
    return stack;
}

} // namespace rs::app::workspace

#include "BlockCommand.h"
#include "CommandLookup.h"

#include <algorithm>
#include <utility>

namespace rs::editor {

AddBlockCommand::AddBlockCommand(rs::world::Block block) : block_(std::move(block)) {}

void AddBlockCommand::Apply(rs::world::TrackGraph& graph) {
    index_ = graph.blocks.size();
    graph.blocks.push_back(block_);
}

void AddBlockCommand::Revert(rs::world::TrackGraph& graph) {
    const size_t index = FindIndexByMember(graph.blocks, &rs::world::Block::guid, block_.guid);
    if (index < graph.blocks.size()) {
        graph.blocks.erase(graph.blocks.begin() + static_cast<std::ptrdiff_t>(index));
    }
}

RemoveBlockCommand::RemoveBlockCommand(rs::world::Guid blockId) : blockId_(blockId) {}

void RemoveBlockCommand::Apply(rs::world::TrackGraph& graph) {
    const size_t index = FindIndexByMember(graph.blocks, &rs::world::Block::guid, blockId_);
    removed_ = index < graph.blocks.size();
    if (removed_) {
        block_ = graph.blocks[index];
        index_ = index;
        graph.blocks.erase(graph.blocks.begin() + static_cast<std::ptrdiff_t>(index));
    }
}

void RemoveBlockCommand::Revert(rs::world::TrackGraph& graph) {
    if (removed_) {
        graph.blocks.insert(
            graph.blocks.begin() + static_cast<std::ptrdiff_t>(std::min(index_, graph.blocks.size())),
            block_);
    }
}

SetBlockPropertyCommand::SetBlockPropertyCommand(rs::world::Guid blockId, BlockPropertyDelta delta)
    : blockId_(blockId), delta_(std::move(delta)) {}

void SetBlockPropertyCommand::Apply(rs::world::TrackGraph& graph) {
    const size_t index = FindIndexByMember(graph.blocks, &rs::world::Block::guid, blockId_);
    hadOld_ = index < graph.blocks.size();
    if (hadOld_) {
        oldValues_ = {};
        ApplyOptionalField(delta_.edges, graph.blocks[index].edges, oldValues_.edges);
        ApplyOptionalField(delta_.name, graph.blocks[index].name, oldValues_.name);
    }
    savedSpeedSignals_.reset();
    if (hadOld_ && delta_.edges.has_value()) {
        savedSpeedSignals_ = graph.speedSignals;
        rs::world::PruneSignalsWithoutOwner(graph);
    }
}

void SetBlockPropertyCommand::Revert(rs::world::TrackGraph& graph) {
    const size_t index = FindIndexByMember(graph.blocks, &rs::world::Block::guid, blockId_);
    if (hadOld_ && index < graph.blocks.size()) {
        RestoreOptionalField(oldValues_.edges, graph.blocks[index].edges);
        RestoreOptionalField(oldValues_.name, graph.blocks[index].name);
    }
    if (savedSpeedSignals_.has_value()) {
        graph.speedSignals = *savedSpeedSignals_;
    }
}

} // namespace rs::editor

#include "SpeedSignalCommand.h"
#include "CommandLookup.h"

#include <algorithm>
#include <utility>
#include "../../world/alignment/TrackGraph.h"

namespace rs::editor {

AddSpeedSignalCommand::AddSpeedSignalCommand(rs::world::SpeedSignal signal)
    : signal_(std::move(signal)) {}

void AddSpeedSignalCommand::Apply(rs::world::TrackGraph& graph) {
    graph.speedSignals.push_back(signal_);
}

void AddSpeedSignalCommand::Revert(rs::world::TrackGraph& graph) {
    const size_t index =
        FindIndexByMember(graph.speedSignals, &rs::world::SpeedSignal::guid, signal_.guid);
    if (index < graph.speedSignals.size()) {
        graph.speedSignals.erase(graph.speedSignals.begin() + static_cast<std::ptrdiff_t>(index));
    }
}

RemoveSpeedSignalCommand::RemoveSpeedSignalCommand(rs::world::Guid signalId) : signalId_(signalId) {}

void RemoveSpeedSignalCommand::Apply(rs::world::TrackGraph& graph) {
    const size_t index =
        FindIndexByMember(graph.speedSignals, &rs::world::SpeedSignal::guid, signalId_);
    removed_ = index < graph.speedSignals.size();
    if (removed_) {
        signal_ = graph.speedSignals[index];
        index_ = index;
        graph.speedSignals.erase(graph.speedSignals.begin() + static_cast<std::ptrdiff_t>(index));
    }
}

void RemoveSpeedSignalCommand::Revert(rs::world::TrackGraph& graph) {
    if (removed_) {
        graph.speedSignals.insert(
            graph.speedSignals.begin() +
                static_cast<std::ptrdiff_t>(std::min(index_, graph.speedSignals.size())),
            signal_);
    }
}

SetSpeedSignalPropertyCommand::SetSpeedSignalPropertyCommand(rs::world::Guid signalId,
                                                             SpeedSignalPropertyDelta delta)
    : signalId_(signalId), delta_(std::move(delta)) {}

void SetSpeedSignalPropertyCommand::Apply(rs::world::TrackGraph& graph) {
    const size_t index =
        FindIndexByMember(graph.speedSignals, &rs::world::SpeedSignal::guid, signalId_);
    hadOld_ = index < graph.speedSignals.size();
    if (hadOld_) {
        rs::world::SpeedSignal& signal = graph.speedSignals[index];
        oldValues_ = {};
        ApplyOptionalField(delta_.edge, signal.edge, oldValues_.edge);
        ApplyOptionalField(delta_.block, signal.block, oldValues_.block);
        ApplyOptionalField(delta_.s, signal.s, oldValues_.s);
        ApplyOptionalField(delta_.direction, signal.direction, oldValues_.direction);
        ApplyOptionalField(delta_.stages, signal.stages, oldValues_.stages);
        ApplyOptionalField(delta_.switchOptions, signal.switchOptions, oldValues_.switchOptions);
    }
}

void SetSpeedSignalPropertyCommand::Revert(rs::world::TrackGraph& graph) {
    const size_t index =
        FindIndexByMember(graph.speedSignals, &rs::world::SpeedSignal::guid, signalId_);
    if (hadOld_ && index < graph.speedSignals.size()) {
        rs::world::SpeedSignal& signal = graph.speedSignals[index];
        RestoreOptionalField(oldValues_.edge, signal.edge);
        RestoreOptionalField(oldValues_.block, signal.block);
        RestoreOptionalField(oldValues_.s, signal.s);
        RestoreOptionalField(oldValues_.direction, signal.direction);
        RestoreOptionalField(oldValues_.stages, signal.stages);
        RestoreOptionalField(oldValues_.switchOptions, signal.switchOptions);
    }
}

} // namespace rs::editor

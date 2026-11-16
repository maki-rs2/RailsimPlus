#include "PointDiaCommand.h"

#include <utility>

#include "../../world/alignment/TrackGraph.h"

namespace rs::editor {

SetPointDiaCommand::SetPointDiaCommand(rs::world::Guid nodeId, PointDiaDelta delta)
    : nodeId_(nodeId), delta_(std::move(delta)) {}

// delta に設定されている項目だけを適用し、変更前の値を oldValues_ へ保存する。
void SetPointDiaCommand::Apply(rs::world::TrackGraph& graph) {
    const auto found = graph.nodes.find(nodeId_);
    if (found == graph.nodes.end()) {
        hadOld_ = false;
        return;
    }
    rs::world::TrackNode& node = found->second;
    oldValues_ = {};
    ApplyOptionalField(delta_.name, node.pointName, oldValues_.name);
    ApplyOptionalField(delta_.stationId, node.pointStationId, oldValues_.stationId);
    ApplyOptionalField(delta_.routine, node.pointRoutine, oldValues_.routine);
    ApplyOptionalField(delta_.formationRoutines, node.pointFormationRoutines, oldValues_.formationRoutines);
    ApplyOptionalField(delta_.tagRoutines, node.pointTagRoutines, oldValues_.tagRoutines);
    ApplyOptionalField(delta_.conditions, node.pointConditions, oldValues_.conditions);
    ApplyOptionalField(delta_.formationConditions, node.pointFormationConditions, oldValues_.formationConditions);
    ApplyOptionalField(delta_.tagConditions, node.pointTagConditions, oldValues_.tagConditions);
    hadOld_ = true;
}

void SetPointDiaCommand::Revert(rs::world::TrackGraph& graph) {
    const auto found = graph.nodes.find(nodeId_);
    if (!hadOld_ || found == graph.nodes.end()) return;
    rs::world::TrackNode& node = found->second;
    RestoreOptionalField(oldValues_.name, node.pointName);
    RestoreOptionalField(oldValues_.stationId, node.pointStationId);
    RestoreOptionalField(oldValues_.routine, node.pointRoutine);
    RestoreOptionalField(oldValues_.formationRoutines, node.pointFormationRoutines);
    RestoreOptionalField(oldValues_.tagRoutines, node.pointTagRoutines);
    RestoreOptionalField(oldValues_.conditions, node.pointConditions);
    RestoreOptionalField(oldValues_.formationConditions, node.pointFormationConditions);
    RestoreOptionalField(oldValues_.tagConditions, node.pointTagConditions);
}

} // namespace rs::editor

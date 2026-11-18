#include "Command.h"

#include "../../world/alignment/BlockRegistry.h"
#include "../../world/alignment/EdgeEndMismatch.h"
#include "../../world/alignment/PlatformRegistry.h"
#include "../../world/alignment/SpeedSignalRegistry.h"
#include "../../world/scene/SceneRegistry.h"

#include <spdlog/spdlog.h>
#include "../../world/alignment/TrackGraph.h"

namespace rs::editor {

using rs::world::SceneRegistry;
using rs::world::TrackGraph;

namespace {

// コマンドの途中の索引を外へ公開せず、適用・復元が終わった状態から一括で作る。
void RebuildCommandIndices(TrackGraph& graph) {
    rs::world::RebuildBlockIndices(graph);
    rs::world::RebuildPlatformIndices(graph);
    rs::world::RebuildSpeedSignalIndices(graph);
}

#ifndef NDEBUG
// デバッグビルドのみ: 全edgeの端が両端ノードの姿勢と一致しているかを調べ、ずれがあれば警告する。
void WarnEdgeEndMismatches(const TrackGraph& graph, const char* where) {
    const std::vector<rs::world::EdgeEndMismatch> mismatches =
        rs::world::FindEdgeEndMismatches(graph);
    for (const rs::world::EdgeEndMismatch& m : mismatches)
        spdlog::warn("[edge-end] {}: edge端がノード姿勢と不一致 ({}側 位置{:.5f}m 向き{:.4f}度)",
                     where, m.atStart ? "始点" : "終点", m.positionErrorM, m.directionErrorDeg);
}
#else
void WarnEdgeEndMismatches(const TrackGraph&, const char*) {}
#endif

} // namespace

bool CommandStack::Execute(TrackGraph& graph, std::unique_ptr<ICommand> cmd) {
    cmd->Apply(graph);
    if (!cmd->Applied() || !cmd->Changed()) {
        cmd->Revert(graph);
        RebuildCommandIndices(graph);
        return false;
    }
    RebuildCommandIndices(graph);
    WarnEdgeEndMismatches(graph, "Execute");
    undoStack_.push_back({std::move(cmd), false});
    redoStack_.clear();
    return true;
}

bool CommandStack::Execute(TrackGraph& graph, SceneRegistry& sceneRegistry,
                           std::unique_ptr<ICommand> cmd) {
    cmd->Apply(sceneRegistry);
    cmd->Apply(graph);
    if (!cmd->Applied() || !cmd->Changed()) {
        cmd->Revert(graph);
        cmd->Revert(sceneRegistry);
        RebuildCommandIndices(graph);
        return false;
    }
    RebuildCommandIndices(graph);
    WarnEdgeEndMismatches(graph, "Execute");
    undoStack_.push_back({std::move(cmd), true});
    redoStack_.clear();
    return true;
}

void CommandStack::Undo(TrackGraph& graph) {
    if (undoStack_.empty()) return;
    Entry entry = std::move(undoStack_.back());
    undoStack_.pop_back();
    entry.command->Revert(graph);
    RebuildCommandIndices(graph);
    WarnEdgeEndMismatches(graph, "Undo");
    redoStack_.push_back(std::move(entry));
}

void CommandStack::Undo(TrackGraph& graph, SceneRegistry& sceneRegistry) {
    if (undoStack_.empty()) return;
    Entry entry = std::move(undoStack_.back());
    undoStack_.pop_back();
    entry.command->Revert(graph);
    if (entry.sceneCommand) entry.command->Revert(sceneRegistry);
    RebuildCommandIndices(graph);
    WarnEdgeEndMismatches(graph, "Undo");
    redoStack_.push_back(std::move(entry));
}

bool CommandStack::Redo(TrackGraph& graph) {
    if (redoStack_.empty()) return false;
    Entry entry = std::move(redoStack_.back());
    redoStack_.pop_back();
    entry.command->Apply(graph);
    if (!entry.command->Applied()) {
        entry.command->Revert(graph);
        RebuildCommandIndices(graph);
        redoStack_.push_back(std::move(entry));
        return false;
    }
    RebuildCommandIndices(graph);
    WarnEdgeEndMismatches(graph, "Redo");
    undoStack_.push_back(std::move(entry));
    return true;
}

bool CommandStack::Redo(TrackGraph& graph, SceneRegistry& sceneRegistry) {
    if (redoStack_.empty()) return false;
    Entry entry = std::move(redoStack_.back());
    redoStack_.pop_back();
    if (entry.sceneCommand) entry.command->Apply(sceneRegistry);
    entry.command->Apply(graph);
    if (!entry.command->Applied()) {
        entry.command->Revert(graph);
        if (entry.sceneCommand) entry.command->Revert(sceneRegistry);
        RebuildCommandIndices(graph);
        redoStack_.push_back(std::move(entry));
        return false;
    }
    RebuildCommandIndices(graph);
    WarnEdgeEndMismatches(graph, "Redo");
    undoStack_.push_back(std::move(entry));
    return true;
}

} // namespace rs::editor

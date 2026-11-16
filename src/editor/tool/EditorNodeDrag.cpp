//
// EditorNodeDrag.cpp - EditorNodeDrag.h の実装
//
#include "EditorNodeDrag.h"
#include "Editor.h"

#include <memory>
#include <utility>

#include "NodeDragCenterline.h"
#include "PlacementRescaleCommand.h"

namespace rs::editor {

namespace {

// 計画を MoveNodeCommand の CompositeCommand として実行する。計画が ok でなければ何もしない。
// edge の spline は MoveNodeCommand が RebuildEdgeSpline で作り直す(計画の spline と同じ入力)。
bool ExecutePlan(Editor& editor,
                 rs::world::ManualPlacementOverlay& overlay, const NodeDragPlan& plan) {
    if (!plan.ok) return false;
    CompositeCommand composite;
    for (const NodePoseChange& change : plan.nodes) {
        composite.Add(std::make_unique<MoveNodeCommand>(change.node, change.pos, change.tangent,
                                                         change.up, change.keepEdgeSigns));
    }
    for (const rs::world::Guid& edge : plan.detachEdges) {
        EdgePropertyDelta delta;
        delta.detachedFromCenterline = true;
        composite.Add(std::make_unique<SetEdgePropertyCommand>(edge, delta));
    }
    // edge の長さが変わるので、配置物の位置も長さの比で追従させる。
    return ExecuteWithPlacementRescale(editor, &overlay,
                                std::make_unique<CompositeCommand>(std::move(composite)));
}

// ok でない計画のプレビューは edges を空にする(呼び出し側の判定を ok だけにする)。
NodeDragPlan ForPreview(NodeDragPlan plan) {
    if (!plan.ok) plan.edges.clear();
    return plan;
}

} // namespace

bool EditorNodeDrag::CanMoveNode(const rs::world::TrackGraph& graph,
                                 const rs::world::Guid& nodeGuid) const {
    return CanMovePlainNode(graph, nodeGuid);
}

bool EditorNodeDrag::CanMoveCenterlineNode(const rs::world::TrackGraph& graph,
                                           const rs::world::Guid& nodeGuid) const {
    return IsCenterlineNodeMovable(graph, nodeGuid);
}

bool EditorNodeDrag::MoveNodeTo(Editor& editor,
                                rs::world::ManualPlacementOverlay& overlay,
                                const rs::world::Guid& nodeGuid, const rs::core::Vec3& newPos) {
    rs::world::TrackGraph& graph = editor.graph;
    return ExecutePlan(editor, overlay, PlanNodeMove(graph, nodeGuid, newPos));
}

bool EditorNodeDrag::SetNodeElevation(Editor& editor,
                                      rs::world::ManualPlacementOverlay& overlay,
                                      const rs::world::Guid& nodeGuid, float elevation) {
    rs::world::TrackGraph& graph = editor.graph;
    const auto nit = graph.nodes.find(nodeGuid);
    if (nit == graph.nodes.end() || nit->second.pos.y == elevation) return false;
    return ExecutePlan(editor, overlay, PlanNodeElevation(graph, nodeGuid, elevation));
}

bool EditorNodeDrag::SetNodeTangent(Editor& editor,
                                      rs::world::ManualPlacementOverlay& overlay,
                                      const rs::world::Guid& nodeGuid,
                                      const rs::core::Vec3& newTangent) const {
    rs::world::TrackGraph& graph = editor.graph;
    return ExecutePlan(editor, overlay, PlanNodeTangent(graph, nodeGuid, newTangent));
}

NodeDragPlan EditorNodeDrag::PreviewMoveNodeTo(const rs::world::TrackGraph& graph,
                                               const rs::world::Guid& nodeGuid,
                                               const rs::core::Vec3& candidatePos) const {
    return ForPreview(PlanNodeMove(graph, nodeGuid, candidatePos));
}

NodeDragPlan EditorNodeDrag::PreviewSetNodeTangent(const rs::world::TrackGraph& graph,
                                                     const rs::world::Guid& nodeGuid,
                                                     const rs::core::Vec3& candidateTangent) const {
    return ForPreview(PlanNodeTangent(graph, nodeGuid, candidateTangent));
}

} // namespace rs::editor

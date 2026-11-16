#include "Editor.h"
#include "../../world/scene/SceneRegistry.h"

namespace rs::editor {

using rs::core::Vec3;
using rs::world::Guid;
using rs::world::NilGuid;

bool Editor::ExecuteCommand(std::unique_ptr<ICommand> command,
                            rs::world::SceneRegistry* sceneRegistry) {
    return ApplyCommand(CommandAction::Execute, std::move(command), sceneRegistry);
}

void Editor::Undo() {
    if (CanUndo()) ApplyCommand(CommandAction::Undo);
}

void Editor::Redo() {
    if (CanRedo()) ApplyCommand(CommandAction::Redo);
}

bool Editor::IsDragging() const {
    return nodeDragUi.active || tangentDragUi.active || placementDragUi.active ||
           sectionEditUi.boundaryDragActive || sceneEntityGizmoDrag.active || boxSelectUi.active;
}

bool Editor::ApplyCommand(CommandAction action, std::unique_ptr<ICommand> command,
                          rs::world::SceneRegistry* sceneRegistry) {
    if (sceneRegistry != nullptr) BindSceneRegistry(*sceneRegistry);
    switch (action) {
    case CommandAction::Execute:
        if (sceneRegistry_ != nullptr) {
            if (!stack.Execute(graph, *sceneRegistry_, std::move(command))) return false;
        } else if (!stack.Execute(graph, std::move(command))) {
            return false;
        }
        break;
    case CommandAction::Undo:
        if (sceneRegistry_ != nullptr) stack.Undo(graph, *sceneRegistry_);
        else stack.Undo(graph);
        break;
    case CommandAction::Redo:
        if (sceneRegistry_ != nullptr) {
            if (!stack.Redo(graph, *sceneRegistry_)) return false;
        } else if (!stack.Redo(graph)) {
            return false;
        }
        break;
    }
    NotifyGraphChanged();
    return true;
}

void Editor::NotifyGraphChanged() {
    ++revision;
    PruneInvalidReferences();
}

void Editor::PruneInvalidReferences() {
    PruneSelectedEdges();
    const auto pruneNode = [this](Guid& id) {
        if (!graph.nodes.contains(id)) id = NilGuid();
    };
    const auto pruneEdge = [this](Guid& id) {
        if (!graph.edges.contains(id)) id = NilGuid();
    };
    pruneNode(selectedNode);
    pruneNode(hoveredNode);
    pruneNode(hoveredPointNode);
    pruneEdge(hoveredEdge);
    if (!graph.nodes.contains(selectPress.node)) selectPress.Clear();
    if (hoveredNode == NilGuid()) {
        hoveredTangentHandle = false;
        hoveredTangentHandleSide = 0;
    }
    const auto hasStation = [this](const Guid& id) {
        return std::ranges::any_of(graph.stations, [&id](const rs::world::StationInfo& item) {
            return item.id == id;
        });
    };
    if (!hasStation(selectedStation)) ClearSelectedStation();
    if (!hasStation(hoveredStation)) hoveredStation = NilGuid();
    if (!graph.platformIndexById.contains(selectedPlatform)) ClearSelectedPlatform();
    if (!graph.platformIndexById.contains(hoveredPlatform)) hoveredPlatform = NilGuid();
    if (!graph.blockIndexById.contains(selectedBlock)) ClearSelectedBlock();
    if (!graph.edges.contains(selectedPlacementEdge)) ClearSelectedPlacement();
    if (!graph.edges.contains(hoveredPlacementEdge)) {
        hoveredPlacementEdge = NilGuid();
        hoveredPlacementS = 0.0;
        hoveredPlacementFolder.clear();
    }
    if (sceneRegistry_ != nullptr) {
        std::vector<Guid> retained;
        for (const Guid& id : selectedSceneEntityIds_.Items()) {
            if (sceneRegistry_->FindEntity(id) != nullptr) retained.push_back(id);
        }
        selectedSceneEntityIds_.SetItems(retained);
        SyncSelectedSceneEntity();
        if (sceneRegistry_->FindEntity(hoveredSceneEntityId) == nullptr) hoveredSceneEntityId = NilGuid();
    }
    if (lastPlacedTravel && !graph.edges.contains(lastPlacedTravel->edge)) lastPlacedTravel.reset();
    if (!graph.edges.contains(speedSignalHover.edge) ||
        (speedSignalHover.block != NilGuid() && !graph.blockIndexById.contains(speedSignalHover.block)))
        speedSignalHover = {};
    if (speedSignalPending && !graph.edges.contains(speedSignalPending->edge)) CancelSpeedSignalPending();
    const auto missingWirePoint = [this](const WirePointRef& point) {
        // 情景から導出する柱はapp側のキャッシュで照合する。ここでは軌道の参照だけを扱う。
        return !IsStructLinePoint(point) && !graph.edges.contains(point.edge);
    };
    if (wireFirstPoint && missingWirePoint(*wireFirstPoint)) wireFirstPoint.reset();
    if (wireHoveredPoint && missingWirePoint(*wireHoveredPoint)) wireHoveredPoint.reset();
    if (wireHoveredSpan && (missingWirePoint(wireHoveredSpan->a) || missingWirePoint(wireHoveredSpan->b)))
        wireHoveredSpan.reset();
    if (wireSelectedSpan && (missingWirePoint(wireSelectedSpan->a) || missingWirePoint(wireSelectedSpan->b)))
        wireSelectedSpan.reset();
    layingSession_.PruneInvalidReferences(graph);
}

// 敷設フロー(解く/プレビュー系・クリック状態機械・確定処理)は
// TrackLayingSession(TrackLayingSession.cpp / TrackLayingSessionSolve.cpp /
// TrackLayingSessionCommit.cpp / TrackLayingSessionCommitMulti.cpp)にある。
// ここには敷設状態(TrackLayingSession)に依存しないEditor本体のヘルパのみ残す。

Guid Editor::FindNearestNodeWorld(const Vec3& pos, float snapRadius) const {
    Guid best = NilGuid();
    float bestDist = snapRadius;
    for (const auto& kv : graph.nodes) {
        float d = (kv.second.pos - pos).Length();
        if (d <= bestDist) {
            bestDist = d;
            best = kv.first;
        }
    }
    return best;
}

// 選択とホバーをすべて解除する(表示モード・編集サブモードの切替、レイアウト読込・新規)。
void Editor::ClearSelectionAndHover() {
    selectedNode = rs::world::NilGuid();
    ClearSelectedStation();
    ClearSelectedPlatform();
    ClearSelectedBlock();
    signalBlockArmedAction = SignalBlockArmedAction::None;
    CancelSpeedSignalPending();
    // パネルが描かれないモードではホバーが更新されないため、ここで落とす。
    hoveredStation = rs::world::NilGuid();
    hoveredPlatform = rs::world::NilGuid();
    hoveredPointNode = rs::world::NilGuid();
    ClearSelectedEdge();
    ClearSelectedPlacement();
    ClearSelectedSceneEntity();
    hoveredSceneEntityId = rs::world::NilGuid();
    hoveredPlacementEdge = rs::world::NilGuid();
    hoveredPlacementFolder.clear();
    // 配置物ドラッグ中にモード切替すると、選択がクリアされた後もドラッグだけ
    // 生きたまま残ってしまう。
    placementDragUi.active = false;
    // ノード・接線ハンドルのドラッグも同じ。掴んだまま別のモードへ移ると、プレビューだけが残る。
    CancelNodeDrag();
    CancelTangentDrag();
    // Railモード以外ではUpdateHoverが呼ばれずホバー状態が上書きされないため、
    // 切替直前のハイライトが残り続ける。
    hoveredNode = rs::world::NilGuid();
    hoveredEdge = rs::world::NilGuid();
    // 架線の1点目と径間の選択もモード切替で捨てる。
    wireFirstPoint.reset();
    ClearWireSpanSelection();
}

} // namespace rs::editor

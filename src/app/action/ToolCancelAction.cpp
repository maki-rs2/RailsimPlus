//
// ToolCancelAction.cpp - Escで取り消せる編集ツール操作の判定と取り消し
//
#include "ToolCancelAction.h"

#include <span>

#include "EdgeSectionEditAction.h"
#include "EditToolScope.h"

namespace rs::app {

namespace {

// 取り消せる操作1種類分。「途中か」と「取り消し」を組にして持つ。判定と取り消しが同じ表を
// 回すので、操作を足すときに片方だけ書き忘れることがない。
struct CancelableOperation {
    bool (*isActive)(const rs::editor::Editor& editor);
    void (*cancel)(rs::editor::Editor& editor);
    bool stopAfterCancel = false;
};

// 信号・閉塞モードの操作。
constexpr CancelableOperation kSignalBlockOperations[] = {
    {[](const rs::editor::Editor& editor) { return editor.speedSignalPending.has_value(); },
     [](rs::editor::Editor& editor) { editor.CancelSpeedSignalPending(); }, true},
    {[](const rs::editor::Editor& editor) {
         return editor.signalBlockArmedAction != rs::editor::SignalBlockArmedAction::None;
     },
     [](rs::editor::Editor& editor) { editor.signalBlockArmedAction = rs::editor::SignalBlockArmedAction::None; }},
};

// 線路編集系モードの操作。取り消す順はこの並びのとおり。
// 編集サブモード(配置物追加・架線編集・区間分割)の取り消しは、パネルのトグルと同じ解除にそろえる
// (配置物追加・架線編集はTrackLayingAction::SetPlacementAddMode/SetWireEditModeがフラグを
// 落とすだけ、区間分割はEdgeSectionEditAction::SetSplitMode)。
// 敷設の途中だけは、取り消しが1回で1段階だけ戻る(Editor::CancelPlacement)。
constexpr CancelableOperation kTrackOperations[] = {
    {[](const rs::editor::Editor& editor) { return editor.IsPlacingArc(); },
     [](rs::editor::Editor& editor) { editor.CancelPlacement(); }},
    {[](const rs::editor::Editor& editor) { return editor.nodeDragUi.active; },
     [](rs::editor::Editor& editor) { editor.CancelNodeDrag(); }},
    {[](const rs::editor::Editor& editor) { return editor.tangentDragUi.active; },
     [](rs::editor::Editor& editor) { editor.CancelTangentDrag(); }},
    {[](const rs::editor::Editor& editor) { return editor.stationAnchorMode; },
     [](rs::editor::Editor& editor) {
         editor.stationAnchorMode = false;
         editor.stationAnchorFolder.clear();
     }},
    {[](const rs::editor::Editor& editor) { return editor.sceneryPlacementMode; },
     [](rs::editor::Editor& editor) {
         editor.sceneryPlacementMode = false;
         editor.sceneryPlacementFolder.clear();
         editor.sceneryPlacementHeightOffset = 0.0f;
     }},
    {[](const rs::editor::Editor& editor) { return editor.placementAddMode; },
     [](rs::editor::Editor& editor) { editor.placementAddMode = false; }},
    // 架線編集モードは取り消しが1回で1段階だけ戻る。1点目・径間の選択があるうちはそれを捨てるだけで、
    // 何も無くなってから次のEscでモードを抜く(下の2行は、1行目が先に評価されるこの並びで成り立つ)。
    {[](const rs::editor::Editor& editor) {
         return editor.wireEditMode && !editor.wireFirstPoint.has_value() &&
                !editor.wireSelectedSpan.has_value();
     },
     [](rs::editor::Editor& editor) { editor.wireEditMode = false; }},
    {[](const rs::editor::Editor& editor) {
         return editor.wireEditMode &&
                (editor.wireFirstPoint.has_value() || editor.wireSelectedSpan.has_value());
     },
     [](rs::editor::Editor& editor) {
         editor.wireFirstPoint.reset();
         editor.ClearWireSpanSelection();
     }},
    {[](const rs::editor::Editor& editor) { return editor.sectionSplitMode; },
     [](rs::editor::Editor& editor) { EdgeSectionEditAction(editor).SetSplitMode(false); }},
};

// そのViewModeで有効な操作の表を返す。有効な編集が無いモードは空。
std::span<const CancelableOperation> OperationsFor(ViewMode viewMode, const rs::editor::Editor& editor) {
    if (IsSignalBlockEditingEnabled(viewMode)) return kSignalBlockOperations;
    if (IsTrackEditingEnabled(viewMode, editor)) return kTrackOperations;
    return {};
}

} // namespace

ToolCancelAction::ToolCancelAction(rs::editor::Editor& editor, ViewMode viewMode)
    : editor_(editor), viewMode_(viewMode) {}

bool ToolCancelAction::HasCancelableOperation() const {
    for (const CancelableOperation& operation : OperationsFor(viewMode_, editor_)) {
        if (operation.isActive(editor_)) return true;
    }
    return false;
}

void ToolCancelAction::CancelOperation() {
    for (const CancelableOperation& operation : OperationsFor(viewMode_, editor_)) {
        if (!operation.isActive(editor_)) continue;
        operation.cancel(editor_);
        if (operation.stopAfterCancel) return;
    }
}

} // namespace rs::app

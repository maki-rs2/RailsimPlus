//
// EditToolScope.h - どの表示モードで編集ツールの入力を受け付けるか
//
// 「編集が有効なモードか」の条件を1か所に置く。ビューポートの入力受付と、Escでの
// ツール取消(ToolCancelAction)が同じ条件を見るため、片方だけ直して食い違わないようにする。
//
#pragma once

#include "../../editor/tool/Editor.h"
#include "../ui/ViewMode.h"

namespace rs::app {

// 敷設・配置系の編集が有効か(Railか、配置プレビュー中の情景)。
inline bool IsTrackEditingEnabled(ViewMode viewMode, const rs::editor::Editor& editor) {
    return GetViewModeTraits(viewMode).trackEditing || (GetViewModeTraits(viewMode).scenery && editor.sceneryPlacementMode);
}

// 信号・閉塞の編集が有効か。
inline bool IsSignalBlockEditingEnabled(ViewMode viewMode) { return GetViewModeTraits(viewMode).signalBlock; }

// Ctrl+Z / Ctrl+Y(Undo/Redo)を受け付けるか。敷設・配置系か信号・閉塞の編集が有効なとき。
// Undoの対象はEditorのコマンド履歴で、どちらのモードも同じ履歴に積む。
inline bool IsUndoRedoEnabled(ViewMode viewMode, const rs::editor::Editor& editor) {
    return IsTrackEditingEnabled(viewMode, editor) ||
           (GetViewModeTraits(viewMode).scenery && (editor.HasSelectedSceneEntity() || editor.sceneryPlacementMode)) ||
           IsSignalBlockEditingEnabled(viewMode);
}

} // namespace rs::app

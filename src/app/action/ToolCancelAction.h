//
// ToolCancelAction.h - Escで取り消せる編集ツール操作の判定と取り消し
//
// 「取り消せる操作があるか」と「取り消し」を1組にして同じ集合を見る(片方だけ直して
// 「何か残ったまま作業モードを抜ける」「Escを押しても何も起きない」状態になるのを防ぐ)。
// 対象はそのViewModeで実際に有効な操作だけ。モード切替では敷設途中の状態が解除されないため、
// 有効でないモードまで見ると、見えない操作を取り消すだけで鑑賞へ戻れなくなる。
//
#pragma once

#include "../../editor/tool/Editor.h"
#include "../ui/ViewMode.h"

namespace rs::app {

class ToolCancelAction {
public:
    ToolCancelAction(rs::editor::Editor& editor, ViewMode viewMode);

    // 取り消せる操作の途中か。
    bool HasCancelableOperation() const;
    // 取り消せる操作を取り消す。信号の仮止めと敷設の途中は1回で1段階戻る。
    void CancelOperation();

private:
    rs::editor::Editor& editor_;
    ViewMode viewMode_;
};

} // namespace rs::app

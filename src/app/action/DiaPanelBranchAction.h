//
// DiaPanelBranchAction.h - ダイヤの左タブの操作のうち、分岐(ポイント)を対象にする分(DiaPanelRoutineAction から呼ぶ)
//
// 画面の添字から分岐のノード・層・要素・条件へ引き直し、PointRoutineAction を呼ぶ。シーンがアクティブでない、
// ノードが消えた、画面の数と実際の数が合わない(画面が古い)ときは何もしない。
//
#pragma once

#include "DiaPanelAction.h"

namespace rs::app::dia_panel_action::branch {

// request.targetIndex_ が指す対象が分岐か。
bool IsBranchTarget(const Refs& refs, const Request& request);

void AddLayer(const Refs& refs, const Request& request);
void DeleteLayer(const Refs& refs, const Request& request);
void ReorderLayer(const Refs& refs, const Request& request);
void AddElement(const Refs& refs, const Request& request);
void DeleteElement(const Refs& refs, const Request& request);
void DisableElement(const Refs& refs, const Request& request);
void ReorderElement(const Refs& refs, const Request& request);

} // namespace rs::app::dia_panel_action::branch

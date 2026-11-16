//
// EditorPanelSectionEdit.h - 敷設エディタパネルの「edge内区間分割」UI(EditorPanel.cppから分離)
//
#pragma once

#include "../action/EdgeSectionEditAction.h"

namespace rs::app {

// 選択edgeのedge内区間分割(プラグイン/速度制限/閉塞の3層)編集UIを描画する。
// editor.selectedEdgeが未選択の場合は何も描画しない。ビューポート側の分割
// クリック/境界ドラッグ操作はここでは実装しない(モードのトグルのみ)。
struct EdgeSectionEditContext {
    EdgeSectionEditAction& action;
};

void DrawEdgeSectionEditSection(const EdgeSectionEditContext& ctx);

} // namespace rs::app

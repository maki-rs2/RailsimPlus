//
// EditorPanelNodeSection.h - 敷設エディタパネルの「選択node プロパティ」UI(EditorPanel.cppから分離)
//
#pragma once

#include "../action/NodePropertyAction.h"

namespace rs::app {

struct NodeSectionContext {
    NodePropertyAction& action;
};

void DrawSelectedNodeSection(const NodeSectionContext& ctx);

} // namespace rs::app

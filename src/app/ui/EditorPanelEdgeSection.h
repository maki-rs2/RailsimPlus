//
// EditorPanelEdgeSection.h - 敷設エディタパネルの「選択edge プロパティ」UI(EditorPanel.cppから分離)
//
#pragma once

#include "../action/EdgePropertyAction.h"
#include "UiPanels.h"

namespace rs::app {

struct EdgeSectionContext {
    EdgePropertyAction& action;
};

void DrawSelectedEdgeSection(const EdgeSectionContext& ctx);

} // namespace rs::app

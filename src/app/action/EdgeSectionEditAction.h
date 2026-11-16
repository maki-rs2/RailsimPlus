//
// EdgeSectionEditAction.h - edge内区間分割編集のアプリサービス
//
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "../../editor/tool/EditorSectionEdit.h"

namespace rs::editor {
class Editor;
}

namespace rs::app {

struct EdgeSectionRowViewModel {
    std::size_t index = 0;
    double sStart = 0.0;
    double sEnd = 0.0;
    bool selected = false;
    std::size_t pluginFolderCount = 0;
    std::string firstPluginFolder;
};

struct EdgeSectionEditViewModel {
    bool visible = false;
    bool isCenterline = false;
    rs::editor::SectionLayer currentLayer = rs::editor::SectionLayer::PluginSet;
    bool splitMode = false;
    bool hasSpline = false;
    double totalLength = 0.0;
    std::vector<EdgeSectionRowViewModel> rows;
    std::size_t selectedIndex = 0;
    bool hasSelectedBoundary = false;
    double selectedBoundaryS = 0.0;
};

class EdgeSectionEditAction {
public:
    explicit EdgeSectionEditAction(rs::editor::Editor& editor);

    void SetSplitMode(bool enabled);
    void SelectSection(std::size_t index);
    void MoveSelectedBoundary(double newS);
    void RemoveSelectedBoundary();
    EdgeSectionEditViewModel ViewModel() const;

private:
    rs::editor::Editor& editor_;
};

} // namespace rs::app

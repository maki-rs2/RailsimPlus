//
// EdgeSectionEditAction.cpp - edge内区間分割編集のアプリサービス
//
#include "EdgeSectionEditAction.h"

#include <utility>

#include "../../editor/tool/Editor.h"
#include "../../world/alignment/TrackGraph.h"

namespace rs::app {

EdgeSectionEditAction::EdgeSectionEditAction(rs::editor::Editor& editor) : editor_(editor) {}

void EdgeSectionEditAction::SetSplitMode(bool enabled) {
    editor_.sectionSplitMode = enabled;
    if (enabled) {
        // 前面の配置モードと同時にONになると入力が競合するため、排他モードを解除する。
        // HandleEditorInputのelse-if連鎖に対応する不変条件としてここで保つ。
        editor_.placementAddMode = false;
        editor_.wireEditMode = false;
    }
}

void EdgeSectionEditAction::SelectSection(std::size_t index) {
    editor_.SetSelectedSectionIndex(editor_.sectionLayer, index);
}

void EdgeSectionEditAction::MoveSelectedBoundary(double newS) {
    const rs::editor::SectionLayer layer = editor_.sectionLayer;
    editor_.MoveSelectedEdgeSectionBoundary(layer, editor_.SelectedSectionIndex(layer), newS);
}

void EdgeSectionEditAction::RemoveSelectedBoundary() {
    const rs::editor::SectionLayer layer = editor_.sectionLayer;
    editor_.RemoveSelectedEdgeSectionBoundary(layer, editor_.SelectedSectionIndex(layer));
}

EdgeSectionEditViewModel EdgeSectionEditAction::ViewModel() const {
    EdgeSectionEditViewModel viewModel;
    const auto edgeIt = editor_.graph.edges.find(editor_.selectedEdge);
    if (edgeIt == editor_.graph.edges.end()) return viewModel;

    const rs::world::TrackEdge& edge = edgeIt->second;
    viewModel.visible = true;
    viewModel.isCenterline = edge.centerlineOnly;
    viewModel.currentLayer = editor_.sectionLayer;
    viewModel.splitMode = editor_.sectionSplitMode;
    viewModel.hasSpline = !edge.spline.empty();
    if (!viewModel.hasSpline) return viewModel;

    viewModel.totalLength = static_cast<double>(edge.spline.back().s);
    viewModel.selectedIndex = editor_.SelectedSectionIndex(viewModel.currentLayer);

    switch (viewModel.currentLayer) {
    case rs::editor::SectionLayer::PluginSet: {
        const std::vector<rs::world::IntervalList<rs::world::PluginSet>::Span>& spans =
            edge.pluginSections.Spans();
        for (std::size_t index = 0; index < spans.size(); ++index) {
            EdgeSectionRowViewModel row;
            row.index = index;
            row.sStart = spans[index].sStart;
            row.sEnd = index + 1 < spans.size() ? spans[index + 1].sStart : viewModel.totalLength;
            row.selected = index == viewModel.selectedIndex;
            row.pluginFolderCount = spans[index].value.plugins.size();
            if (!spans[index].value.plugins.empty()) row.firstPluginFolder = spans[index].value.plugins.front();
            viewModel.rows.push_back(std::move(row));
        }
        break;
    }
    }

    if (viewModel.selectedIndex < viewModel.rows.size() && viewModel.selectedIndex >= 1) {
        viewModel.hasSelectedBoundary = true;
        viewModel.selectedBoundaryS = viewModel.rows[viewModel.selectedIndex].sStart;
    }
    return viewModel;
}

} // namespace rs::app

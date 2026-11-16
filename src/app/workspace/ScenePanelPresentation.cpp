#include "ScenePanelPresentation.h"

#include <algorithm>
#include <utility>

namespace rs::app::workspace {

bool ScenePanelPresentation::ValidRow(int row) const {
    return row >= 0 && static_cast<std::size_t>(row) < rows_.size();
}

void ScenePanelPresentation::RefreshDerived() {
    canAdd_ = !surfaceChoices_.empty();
    addSelection_ = surfaceChoices_.empty() ? 0 : std::clamp(addSelection_, 0, static_cast<int>(surfaceChoices_.size()) - 1);
    canDelete_ = rows_.size() > 1;
    showArrows_ = canDelete_;
    currentName_.clear();
    for (const ScenePanelRow& row : rows_) {
        if (row.current_) currentName_ = row.name_;
    }
}

bool ScenePanelPresentation::BeginRename(int row) {
    if (!ValidRow(row)) return false;
    CancelRename();
    editingRow_ = row;
    rows_[row].editing_ = true;
    rows_[row].draft_ = rows_[row].name_;
    return true;
}

std::optional<SceneRenameRequest> ScenePanelPresentation::CommitRename() {
    if (!ValidRow(editingRow_)) return std::nullopt;
    const SceneRenameRequest request{editingRow_, rows_[editingRow_].draft_};
    CancelRename();
    if (request.name_.empty()) return std::nullopt;
    return request;
}

void ScenePanelPresentation::CancelRename() {
    if (ValidRow(editingRow_)) {
        ScenePanelRow& row = rows_[editingRow_];
        row.editing_ = false;
        row.draft_ = row.name_;
    }
    editingRow_ = -1;
}

void ScenePanelPresentation::OpenAdd() {
    CancelRename();
    adding_ = true;
    addSelection_ = 0;
}

void ScenePanelPresentation::CloseAdd() { adding_ = false; }

bool ScenePanelPresentation::SelectAddSurface(int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= surfaceChoices_.size()) return false;
    addSelection_ = index;
    return true;
}

const SceneSurfaceChoice* ScenePanelPresentation::SelectedAddSurface() const {
    if (addSelection_ < 0 || static_cast<std::size_t>(addSelection_) >= surfaceChoices_.size()) return nullptr;
    return &surfaceChoices_[addSelection_];
}

bool ScenePanelPresentation::OpenDelete(int row, std::string message) {
    if (!canDelete_ || !ValidRow(row)) return false;
    deletingRow_ = row;
    deleteMessage_ = std::move(message);
    return true;
}

void ScenePanelPresentation::CloseDelete() {
    deletingRow_ = -1;
    deleteMessage_.clear();
}

bool ScenePanelPresentation::CloseTransient() {
    if (adding_) {
        CloseAdd();
        return true;
    }
    if (editingRow_ >= 0) {
        CancelRename();
        return true;
    }
    return false;
}
} // namespace rs::app::workspace

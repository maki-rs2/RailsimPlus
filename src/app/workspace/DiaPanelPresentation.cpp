#include "DiaPanelPresentation.h"

#include <algorithm>
#include <utility>

#include "DiaPanelFormat.h"

namespace rs::app::workspace {
namespace {
template <typename T>
bool Contains(const std::vector<T>& values, int index) {
    return index >= 0 && static_cast<std::size_t>(index) < values.size();
}
} // namespace

void DiaPanelPresentation::SetActive(bool active) {
    if (active_ == active) return;
    active_ = active;
    if (active) return;
    CloseRows();
    ClearHover();
    EndRename();
    for (DiaPanelTarget& target : targets_) {
        target.selected_ = false;
        for (DiaPanelLayer& layer : target.layers_) layer.selected_ = false;
    }
    screen_ = 0;
    targetIndex_ = layerIndex_ = elementIndex_ = dragIndex_ = -1;
    dragKind_.clear();
    addingLayer_ = addingOverwrite_ = false;
    breadcrumb_.clear();
}

bool DiaPanelPresentation::OpenTarget(int index) {
    if (!Contains(targets_, index)) return false;
    CloseRows();
    ClearHover();
    EndRename();
    for (DiaPanelTarget& target : targets_) {
        target.selected_ = false;
        for (DiaPanelLayer& layer : target.layers_) layer.selected_ = false;
    }
    targetIndex_ = index;
    layerIndex_ = elementIndex_ = -1;
    targets_[index].selected_ = true;
    for (DiaPanelListRow& row : rows_) row.highlighted_ = row.kind_ == "target" && row.target_ == index;
    screen_ = 1;
    addingLayer_ = addingOverwrite_ = false;
    UpdateBreadcrumb();
    RefreshChoices();
    return true;
}

bool DiaPanelPresentation::OpenLayer(int index) {
    if (!Contains(targets_, targetIndex_)) return false;
    DiaPanelTarget& target = targets_[targetIndex_];
    if (!Contains(target.layers_, index)) return false;
    const bool wasOpen = layerIndex_ == index;
    CloseRows();
    ClearHover();
    for (DiaPanelLayer& layer : target.layers_) layer.selected_ = false;
    // 開いている層をもう一度選ぶと畳む。開けるのは1層だけ。
    layerIndex_ = wasOpen ? -1 : index;
    target.layers_[index].selected_ = !wasOpen;
    elementIndex_ = -1;
    screen_ = 1;
    addingLayer_ = addingOverwrite_ = false;
    UpdateBreadcrumb();
    RefreshChoices();
    return true;
}

void DiaPanelPresentation::Back() {
    CloseRows();
    ClearHover();
    addingLayer_ = addingOverwrite_ = false;
    if (screen_ == 2) {
        elementIndex_ = -1;
        screen_ = 1;
    } else if (screen_ == 1) {
        for (DiaPanelLayer& layer : targets_[targetIndex_].layers_) layer.selected_ = false;
        layerIndex_ = -1;
        targets_[targetIndex_].selected_ = false;
        targetIndex_ = -1;
        screen_ = 0;
    }
    UpdateBreadcrumb();
    RefreshChoices();
}

void DiaPanelPresentation::BackToList() {
    while (screen_ != 0) Back();
}

DiaPanelLayer* DiaPanelPresentation::CurrentLayer() {
    if (!Contains(targets_, targetIndex_)) return nullptr;
    std::vector<DiaPanelLayer>& layers = targets_[targetIndex_].layers_;
    return Contains(layers, layerIndex_) ? &layers[layerIndex_] : nullptr;
}

void DiaPanelPresentation::CloseRows() {
    DiaPanelLayer* const layer = CurrentLayer();
    if (layer == nullptr) return;
    for (DiaPanelElement& element : layer->elements_) element.open_ = false;
    for (DiaPanelCondition& condition : layer->conditions_) condition.open_ = false;
    addingOverwrite_ = false;
}

bool DiaPanelPresentation::OpenElement(int index) {
    DiaPanelLayer* const layer = CurrentLayer();
    if (layer == nullptr || !Contains(layer->elements_, index)) return false;
    CloseRows();
    ClearHover();
    layer->elements_[index].open_ = true;
    elementIndex_ = index;
    screen_ = 2;
    UpdateBreadcrumb();
    RefreshChoices();
    return true;
}

void DiaPanelPresentation::ToggleCondition(int index) {
    DiaPanelLayer* const layer = CurrentLayer();
    if (layer == nullptr || !Contains(layer->conditions_, index)) return;
    const bool open = !layer->conditions_[index].open_;
    CloseRows();
    layer->conditions_[index].open_ = open;
}

void DiaPanelPresentation::ToggleTimes(int index) {
    DiaPanelLayer* const layer = CurrentLayer();
    if (layer != nullptr && Contains(layer->elements_, index)) {
        layer->elements_[index].timesOpen_ = !layer->elements_[index].timesOpen_;
    }
}

void DiaPanelPresentation::ToggleFold(int index) {
    if (!Contains(rows_, index) || (rows_[index].kind_ != "scene" && rows_[index].kind_ != "station")) return;
    rows_[index].collapsed_ = !rows_[index].collapsed_;
    RecomputeRowVisibility();
}

void DiaPanelPresentation::RecomputeRowVisibility() {
    int hiddenBelowDepth = -1;
    for (DiaPanelListRow& row : rows_) {
        if (hiddenBelowDepth >= 0 && row.depth_ > hiddenBelowDepth) {
            row.visible_ = false;
            continue;
        }
        hiddenBelowDepth = -1;
        row.visible_ = true;
        if (row.collapsed_) hiddenBelowDepth = row.depth_;
    }
}

void DiaPanelPresentation::BeginRename(int index) {
    if (!Contains(rows_, index) || rows_[index].kind_ != "station") return;
    EndRename();
    rows_[index].editing_ = true;
    rows_[index].draft_ = rows_[index].name_;
}

void DiaPanelPresentation::EndRename() {
    for (DiaPanelListRow& row : rows_) row.editing_ = false;
}

void DiaPanelPresentation::ClearHover() {
    for (DiaPanelListRow& row : rows_) row.hovered_ = false;
    for (DiaPanelTarget& target : targets_) {
        for (DiaPanelLayer& layer : target.layers_) {
            layer.hovered_ = false;
            for (DiaPanelElement& element : layer.elements_) {
                element.hovered_ = false;
                for (DiaPanelField& time : element.times_) time.hovered_ = false;
            }
        }
    }
}

void DiaPanelPresentation::UpdateBreadcrumb() {
    breadcrumb_ = Contains(targets_, targetIndex_) ? targets_[targetIndex_].breadcrumb_ : std::string();
    inspectorTitle_.clear();
    // 層は番線の画面の行の中で開くので、見出しに出すのは要素編集のときだけ。
    DiaPanelLayer* const layer = screen_ == 2 ? CurrentLayer() : nullptr;
    if (layer == nullptr) return;
    inspectorTitle_ = layer->name_;
    if (!Contains(layer->elements_, elementIndex_)) return;
    const DiaPanelElement& element = layer->elements_[elementIndex_];
    const std::string operation = element.summary_.substr(0, element.summary_.find(' '));
    inspectorTitle_ += " > " + std::to_string(elementIndex_ + 1) + " " + operation;
}

void DiaPanelPresentation::RefreshChoices() {
    layerChoices_.clear();
    if (Contains(targets_, targetIndex_)) {
        const DiaPanelTarget& target = targets_[targetIndex_];
        for (const DiaPanelChoice& candidate : layerCatalog_) {
            const bool exists = std::any_of(target.layers_.begin(), target.layers_.end(), [&candidate](const DiaPanelLayer& layer) {
                return layer.routineKey_ == candidate.key_ && layer.formationId_ == candidate.id_;
            });
            if (!exists) layerChoices_.push_back(candidate);
        }
    }
    overwriteChoices_.clear();
    DiaPanelLayer* const layer = CurrentLayer();
    if (layer == nullptr || !Contains(layer->elements_, elementIndex_)) return;
    const DiaPanelElement& element = layer->elements_[elementIndex_];
    for (const DiaRunInfoFieldName& name : kDiaRunInfoFieldNames) {
        const bool exists = std::any_of(element.overwrites_.begin(), element.overwrites_.end(), [&name](const DiaPanelField& field) {
            return field.key_ == name.key;
        });
        if (!exists) overwriteChoices_.push_back({name.label, false, name.key, {}});
    }
}

void DiaPanelPresentation::RefreshDerived() {
    for (DiaPanelTarget& target : targets_) {
        for (DiaPanelLayer& layer : target.layers_) {
            layer.summary_.clear();
            layer.empty_ = true;
            for (const DiaPanelElement& element : layer.elements_) {
                if (element.disabled_) continue;
                if (!layer.empty_) layer.summary_ += " → ";
                layer.summary_ += element.summary_;
                layer.empty_ = false;
            }
            if (layer.empty_) layer.summary_ = "空 — 次の層へ";
        }
    }
}

} // namespace rs::app::workspace

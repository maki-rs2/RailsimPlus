#include "RmlUiDiaPanelModel.h"

#include <RmlUi/Core/DataStructHandle.h>
#include <RmlUi/Core/Traits.h>

#include "../workspace/DiaPanelPresentation.h"

namespace rs::app::rmlui {
namespace {
using namespace workspace;

void RegisterRows(Rml::DataModelConstructor& constructor) {
    Rml::StructHandle<DiaPanelChoice> choice = constructor.RegisterStruct<DiaPanelChoice>();
    choice.RegisterMember("value", &DiaPanelChoice::value_);
    choice.RegisterMember("selected", &DiaPanelChoice::selected_);
    constructor.RegisterArray<std::vector<DiaPanelChoice>>();
    Rml::StructHandle<DiaPanelField> field = constructor.RegisterStruct<DiaPanelField>();
    field.RegisterMember("label", &DiaPanelField::label_);
    field.RegisterMember("draft", &DiaPanelField::draft_);
    field.RegisterMember("choices", &DiaPanelField::choices_);
    field.RegisterMember("hovered", &DiaPanelField::hovered_);
    constructor.RegisterArray<std::vector<DiaPanelField>>();
    Rml::StructHandle<DiaPanelElement> element = constructor.RegisterStruct<DiaPanelElement>();
    element.RegisterMember("summary", &DiaPanelElement::summary_);
    element.RegisterMember("overwrite", &DiaPanelElement::overwrite_);
    element.RegisterMember("disabled", &DiaPanelElement::disabled_);
    element.RegisterMember("open", &DiaPanelElement::open_);
    element.RegisterMember("hovered", &DiaPanelElement::hovered_);
    element.RegisterMember("operation", &DiaPanelElement::operation_);
    element.RegisterMember("departure", &DiaPanelElement::departure_);
    element.RegisterMember("seconds", &DiaPanelElement::seconds_);
    element.RegisterMember("times", &DiaPanelElement::times_);
    element.RegisterMember("timesOpen", &DiaPanelElement::timesOpen_);
    element.RegisterMember("connection", &DiaPanelElement::connection_);
    element.RegisterMember("platforms", &DiaPanelElement::platforms_);
    element.RegisterMember("maximum", &DiaPanelElement::maximum_);
    element.RegisterMember("stopPosition", &DiaPanelElement::stopPosition_);
    element.RegisterMember("overwrites", &DiaPanelElement::overwrites_);
    element.RegisterMember("hand", &DiaPanelElement::hand_);
    element.RegisterMember("next", &DiaPanelElement::next_);
    constructor.RegisterArray<std::vector<DiaPanelElement>>();
    Rml::StructHandle<DiaPanelCondition> condition = constructor.RegisterStruct<DiaPanelCondition>();
    condition.RegisterMember("kind", &DiaPanelCondition::kind_);
    condition.RegisterMember("summary", &DiaPanelCondition::summary_);
    condition.RegisterMember("open", &DiaPanelCondition::open_);
    condition.RegisterMember("start", &DiaPanelCondition::start_);
    condition.RegisterMember("end", &DiaPanelCondition::end_);
    condition.RegisterMember("blocks", &DiaPanelCondition::blocks_);
    condition.RegisterMember("all", &DiaPanelCondition::all_);
    condition.RegisterMember("hand", &DiaPanelCondition::hand_);
    constructor.RegisterArray<std::vector<DiaPanelCondition>>();
}

void RegisterHierarchy(Rml::DataModelConstructor& constructor) {
    Rml::StructHandle<DiaPanelLayer> layer = constructor.RegisterStruct<DiaPanelLayer>();
    layer.RegisterMember("name", &DiaPanelLayer::name_);
    layer.RegisterMember("kind", &DiaPanelLayer::kind_);
    layer.RegisterMember("deletable", &DiaPanelLayer::deletable_);
    layer.RegisterMember("reorderable", &DiaPanelLayer::reorderable_);
    layer.RegisterMember("selected", &DiaPanelLayer::selected_);
    layer.RegisterMember("hovered", &DiaPanelLayer::hovered_);
    layer.RegisterMember("conditions", &DiaPanelLayer::conditions_);
    layer.RegisterMember("elements", &DiaPanelLayer::elements_);
    layer.RegisterMember("summary", &DiaPanelLayer::summary_);
    layer.RegisterMember("empty", &DiaPanelLayer::empty_);
    constructor.RegisterArray<std::vector<DiaPanelLayer>>();
    Rml::StructHandle<DiaPanelTarget> target = constructor.RegisterStruct<DiaPanelTarget>();
    target.RegisterMember("branch", &DiaPanelTarget::branch_);
    target.RegisterMember("selected", &DiaPanelTarget::selected_);
    target.RegisterMember("doorLeft", &DiaPanelTarget::doorLeft_);
    target.RegisterMember("doorRight", &DiaPanelTarget::doorRight_);
    target.RegisterMember("opening", &DiaPanelTarget::opening_);
    target.RegisterMember("layers", &DiaPanelTarget::layers_);
    constructor.RegisterArray<std::vector<DiaPanelTarget>>();
    Rml::StructHandle<DiaPanelListRow> row = constructor.RegisterStruct<DiaPanelListRow>();
    row.RegisterMember("kind", &DiaPanelListRow::kind_);
    row.RegisterMember("name", &DiaPanelListRow::name_);
    row.RegisterMember("target", &DiaPanelListRow::target_);
    row.RegisterMember("gray", &DiaPanelListRow::gray_);
    row.RegisterMember("highlighted", &DiaPanelListRow::highlighted_);
    row.RegisterMember("hovered", &DiaPanelListRow::hovered_);
    row.RegisterMember("editing", &DiaPanelListRow::editing_);
    row.RegisterMember("draft", &DiaPanelListRow::draft_);
    row.RegisterMember("draggable", &DiaPanelListRow::draggable_);
    row.RegisterMember("depth", &DiaPanelListRow::depth_);
    row.RegisterMember("collapsed", &DiaPanelListRow::collapsed_);
    row.RegisterMember("visible", &DiaPanelListRow::visible_);
    constructor.RegisterArray<std::vector<DiaPanelListRow>>();
    Rml::StructHandle<DiaPanelPresentation> panel = constructor.RegisterStruct<DiaPanelPresentation>();
    panel.RegisterMember("active", &DiaPanelPresentation::active_);
    panel.RegisterMember("screen", &DiaPanelPresentation::screen_);
    panel.RegisterMember("sceneCount", &DiaPanelPresentation::sceneCount_);
    panel.RegisterMember("breadcrumb", &DiaPanelPresentation::breadcrumb_);
    panel.RegisterMember("inspectorTitle", &DiaPanelPresentation::inspectorTitle_);
    panel.RegisterMember("rows", &DiaPanelPresentation::rows_);
    panel.RegisterMember("targets", &DiaPanelPresentation::targets_);
    panel.RegisterMember("layerChoices", &DiaPanelPresentation::layerChoices_);
    panel.RegisterMember("overwriteChoices", &DiaPanelPresentation::overwriteChoices_);
    panel.RegisterMember("addingLayer", &DiaPanelPresentation::addingLayer_);
    panel.RegisterMember("addingOverwrite", &DiaPanelPresentation::addingOverwrite_);
}

} // namespace

void RegisterDiaPanelModel(Rml::DataModelConstructor& constructor) {
    RegisterRows(constructor);
    RegisterHierarchy(constructor);
}

} // namespace rs::app::rmlui

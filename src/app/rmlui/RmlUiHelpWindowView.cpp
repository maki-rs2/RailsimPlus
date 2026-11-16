#include "RmlUiHelpWindowView.h"

#include <RmlUi/Core/DataStructHandle.h>

#include "../action/HelpAction.h"
#include "../workspace/HelpWindowPlaceholder.h"

namespace rs::app::rmlui {
using namespace workspace;

RmlUiHelpWindowView::RmlUiHelpWindowView(ToastQueue& queue)
    : queue_(queue), presentation_(MakeHelpWindowPlaceholder()) {}

void RmlUiHelpWindowView::RegisterModel(Rml::DataModelConstructor& constructor) {
    Rml::StructHandle<HelpOperationRow> row = constructor.RegisterStruct<HelpOperationRow>();
    row.RegisterMember("operation", &HelpOperationRow::operation_);
    row.RegisterMember("mouse", &HelpOperationRow::mouse_);
    row.RegisterMember("key", &HelpOperationRow::key_);
    constructor.RegisterArray<std::vector<HelpOperationRow>>();
    Rml::StructHandle<HelpOperationTable> table = constructor.RegisterStruct<HelpOperationTable>();
    table.RegisterMember("modeName", &HelpOperationTable::modeName_);
    table.RegisterMember("rows", &HelpOperationTable::rows_);
    constructor.Bind("helpTable", &presentation_.current_);
    constructor.BindEventCallback("help_tutorial", [this](Rml::DataModelHandle, Rml::Event&,
        const Rml::VariantList&) { help_action::OpenTutorial(queue_); });
    constructor.BindEventCallback("help_open", [this](Rml::DataModelHandle, Rml::Event&,
        const Rml::VariantList&) { help_action::OpenHelp(queue_); });
    modelHandle_ = constructor.GetModelHandle();
}

void RmlUiHelpWindowView::Refresh(const WorkspaceSnapshot& snapshot) {
    if (presentation_.SelectContext(snapshot) && modelHandle_) modelHandle_.DirtyVariable("helpTable");
}

} // namespace rs::app::rmlui

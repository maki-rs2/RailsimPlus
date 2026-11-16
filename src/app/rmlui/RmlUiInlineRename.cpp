#include "RmlUiInlineRename.h"

#include <utility>

#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/ID.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/Traits.h>

#include "RmlUiUpdateVariable.h"

namespace rs::app::rmlui {

RmlUiInlineRename::RmlUiInlineRename(const char* editingIdKey, const char* inputSelector)
    : editingIdKey_(editingIdKey), inputSelector_(inputSelector) {}

void RmlUiInlineRename::RegisterModel(Rml::DataModelConstructor& constructor) {
    constructor.Bind(editingIdKey_, &editingId_);
    modelHandle_ = constructor.GetModelHandle();
}

void RmlUiInlineRename::Begin(const std::string& id, const rs::world::Guid& target, std::string initialName,
                              Rml::ElementDocument* document) {
    target_ = target;
    initialName_ = std::move(initialName);
    document_ = document;
    pendingFocus_ = true;
    editingId_ = id;
    modelHandle_.DirtyVariable(editingIdKey_);
}

void RmlUiInlineRename::End() {
    pendingFocus_ = false;
    target_ = rs::world::NilGuid();
    UpdateVariable(modelHandle_, editingIdKey_, editingId_, Rml::String());
}

void RmlUiInlineRename::FocusPending(Rml::ElementDocument* document) {
    if (document != nullptr) document_ = document;
    if (!pendingFocus_ || document_ == nullptr) return;
    Rml::ElementFormControlInput* const input =
        rmlui_dynamic_cast<Rml::ElementFormControlInput*>(document_->QuerySelector(inputSelector_));
    if (input == nullptr) return;
    pendingFocus_ = false;
    input->SetValue(initialName_);
    input->Focus();
    input->Select();
}

std::optional<RmlUiInlineRename::Commit> RmlUiInlineRename::OnCommit(Rml::Event& event) {
    if (!Active()) return std::nullopt;
    Rml::ElementFormControl* const control = rmlui_dynamic_cast<Rml::ElementFormControl*>(event.GetCurrentElement());
    if (control == nullptr) return std::nullopt;
    if (event.GetId() != Rml::EventId::Blur && !event.GetParameter<bool>("linebreak", false)) return std::nullopt;
    Commit commit{target_, control->GetValue()};
    End();
    return commit;
}

void RmlUiInlineRename::OnKey(Rml::Event& event) {
    if (event.GetParameter<int>("key_identifier", 0) != static_cast<int>(Rml::Input::KI_ESCAPE)) return;
    End();
    event.StopPropagation();
}

} // namespace rs::app::rmlui

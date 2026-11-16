#include "RmlUiFieldCommit.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/Input.h>

namespace rs::app::rmlui {

namespace {
// 対象の欄に付ける属性。
constexpr const char* kCommitAttribute = "data-edit-commit";
// フォーカスを受けたときの値(Esc で戻す先)と、Esc で外した印。
constexpr const char* kOriginalAttribute = "data-edit-original";
constexpr const char* kCancelAttribute = "data-edit-cancelled";
} // namespace

RmlUiFieldCommit::RmlUiFieldCommit(Rml::ElementDocument& document) : document_(document) {
    document_.AddEventListener(Rml::EventId::Focus, this, true);
    document_.AddEventListener(Rml::EventId::Keydown, this, true);
}

RmlUiFieldCommit::~RmlUiFieldCommit() {
    document_.RemoveEventListener(Rml::EventId::Focus, this, true);
    document_.RemoveEventListener(Rml::EventId::Keydown, this, true);
}

void RmlUiFieldCommit::ProcessEvent(Rml::Event& event) {
    Rml::Element* const element = event.GetTargetElement();
    if (element == nullptr || !element->HasAttribute(kCommitAttribute)) return;
    Rml::ElementFormControl* const control = rmlui_dynamic_cast<Rml::ElementFormControl*>(element);
    if (control == nullptr) return;
    if (event.GetId() == Rml::EventId::Focus) {
        element->SetAttribute(kOriginalAttribute, control->GetValue());
        element->RemoveAttribute(kCancelAttribute);
        return;
    }
    const Rml::Input::KeyIdentifier key = static_cast<Rml::Input::KeyIdentifier>(event.GetParameter<int>("key_identifier", 0));
    if (key == Rml::Input::KI_RETURN || key == Rml::Input::KI_NUMPADENTER) {
        element->Blur();
        event.StopPropagation();
    } else if (key == Rml::Input::KI_ESCAPE) {
        control->SetValue(element->GetAttribute<Rml::String>(kOriginalAttribute, control->GetValue()));
        element->SetAttribute(kCancelAttribute, "");
        element->Blur();
        event.StopPropagation();
    }
}

bool ConsumeFieldCancel(Rml::Element* element) {
    if (element == nullptr || !element->HasAttribute(kCancelAttribute)) return false;
    element->RemoveAttribute(kCancelAttribute);
    return true;
}

} // namespace rs::app::rmlui

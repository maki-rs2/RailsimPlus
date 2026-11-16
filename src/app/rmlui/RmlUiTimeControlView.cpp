#include "RmlUiTimeControlView.h"

#include <RmlUi/Core/DataStructHandle.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/ID.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/Traits.h>

#include "RmlUiDataEventArgs.h"
#include "RmlUiNumberParse.h"
#include "../action/TimeControlAction.h"
#include "../action/TimeControlViewModel.h"
#include "../workspace/TimeControlTuning.h"

namespace rs::app::rmlui {
using namespace workspace;

RmlUiTimeControlView::RmlUiTimeControlView(ToastQueue& queue)
    : queue_(queue) {
    // 日時・倍率の文字は、Attach後の最初のRefreshで時計の値から入る。
    presentation_.steps_ = MakeTimeControlSteps(TimeControlTuning::Embedded());
}

void RmlUiTimeControlView::RegisterModel(Rml::DataModelConstructor& constructor) {
    Rml::StructHandle<TimeControlStep> step = constructor.RegisterStruct<TimeControlStep>();
    step.RegisterMember("label", &TimeControlStep::label_);
    step.RegisterMember("selected", &TimeControlStep::selected_);
    step.RegisterMember("pause", &TimeControlStep::pause_);
    step.RegisterMember("arrows", &TimeControlStep::arrows_);
    constructor.RegisterArray<std::vector<TimeControlStep>>();
    Rml::StructHandle<TimeControlPresentation> control = constructor.RegisterStruct<TimeControlPresentation>();
    control.RegisterMember("steps", &TimeControlPresentation::steps_);
    control.RegisterMember("scaleLabel", &TimeControlPresentation::scaleLabel_);
    control.RegisterMember("scaleDraft", &TimeControlPresentation::scaleDraft_);
    control.RegisterMember("editingScale", &TimeControlPresentation::editingScale_);
    control.RegisterMember("windowOpen", &TimeControlPresentation::windowOpen_);
    control.RegisterMember("timeText", &TimeControlPresentation::timeText_);
    control.RegisterMember("secondText", &TimeControlPresentation::secondText_);
    control.RegisterMember("dateText", &TimeControlPresentation::dateText_);
    control.RegisterMember("yearDraft", &TimeControlPresentation::yearDraft_);
    control.RegisterMember("monthDraft", &TimeControlPresentation::monthDraft_);
    control.RegisterMember("dayDraft", &TimeControlPresentation::dayDraft_);
    control.RegisterMember("hourDraft", &TimeControlPresentation::hourDraft_);
    control.RegisterMember("minuteDraft", &TimeControlPresentation::minuteDraft_);
    constructor.Bind("timeControl", &presentation_);
    constructor.BindEventCallback("time_ui", [this](Rml::DataModelHandle, Rml::Event& event,
        const Rml::VariantList& arguments) { OnUi(event, arguments); });
    constructor.BindEventCallback("time_edit", [this](Rml::DataModelHandle, Rml::Event& event,
        const Rml::VariantList&) { OnEdit(event); });
    constructor.BindEventCallback("time_window_key", [this](Rml::DataModelHandle, Rml::Event& event,
        const Rml::VariantList&) {
        // 入力欄でEnterを押すとchange(linebreak)が来る。値は入力欄ごとの束縛が先に下書きへ写している。
        if (event.GetId() == Rml::EventId::Change) {
            if (!event.GetParameter<bool>("linebreak", false) || !presentation_.windowOpen_) return;
            ConfirmWindow();
            event.StopPropagation();
            return;
        }
        if (event.GetParameter<int>("key_identifier", 0) != static_cast<int>(Rml::Input::KI_ESCAPE)) return;
        presentation_.windowOpen_ = false;
        event.GetTargetElement()->Blur();
        event.StopPropagation();
        modelHandle_.DirtyVariable("timeControl");
    });
    modelHandle_ = constructor.GetModelHandle();
}

void RmlUiTimeControlView::SetVisible(bool visible) {
    if (visible) return;
    presentation_.CancelScaleEdit();
    presentation_.windowOpen_ = false;
    pendingFocus_ = false;
    modelHandle_.DirtyVariable("timeControl");
}

void RmlUiTimeControlView::Refresh() {
    if (refs_.has_value() && FillTimeControlFromClock(presentation_, refs_->world.gameClock)) {
        modelHandle_.DirtyVariable("timeControl");
    }
    if (!pendingFocus_ || document_ == nullptr) return;
    Rml::Element* const element = document_->QuerySelector(".time-scale-input.is-editing");
    Rml::ElementFormControlInput* const input = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(element);
    if (input == nullptr || !input->IsVisible()) return;
    pendingFocus_ = false;
    input->SetValue(presentation_.scaleDraft_);
    input->Focus();
    input->Select();
}

bool RmlUiTimeControlView::CloseTransient() {
    if (!presentation_.CloseTransient()) return false;
    pendingFocus_ = false;
    modelHandle_.DirtyVariable("timeControl");
    return true;
}

void RmlUiTimeControlView::CloseWindow() {
    if (!presentation_.windowOpen_) return;
    presentation_.windowOpen_ = false;
    modelHandle_.DirtyVariable("timeControl");
}

void RmlUiTimeControlView::OnUi(Rml::Event& event, const Rml::VariantList& arguments) {
    event.StopPropagation();
    const std::string kind = EventArgumentAt<std::string>(arguments, 0, "");
    if (kind == "scale") {
        presentation_.BeginScaleEdit();
        document_ = event.GetCurrentElement()->GetOwnerDocument();
        pendingFocus_ = true;
    } else if (kind == "step") {
        const int index = EventArgumentAt<int>(arguments, 1, -1);
        if (index < 0 || static_cast<std::size_t>(index) >= presentation_.steps_.size()) return;
        if (!refs_.has_value()) return;
        GameSettingsAction action(*refs_);
        if (presentation_.steps_[index].pause_) time_control_action::Pause(action);
        else time_control_action::SetScale(action, presentation_.steps_[index].scale_);
    } else if (kind == "window") {
        if (presentation_.windowOpen_) presentation_.windowOpen_ = false;
        else presentation_.OpenWindow();
    }
    else if (kind == "cancel") presentation_.windowOpen_ = false;
    else if (kind == "confirm" && presentation_.windowOpen_) ConfirmWindow();
    modelHandle_.DirtyVariable("timeControl");
}

void RmlUiTimeControlView::ConfirmWindow() {
    if (refs_.has_value()) {
        GameSettingsAction action(*refs_);
        time_control_action::ChangeDateTime(action, queue_, presentation_.DateDraft(), presentation_.TimeDraft());
    }
    presentation_.windowOpen_ = false;
    modelHandle_.DirtyVariable("timeControl");
}

void RmlUiTimeControlView::OnEdit(Rml::Event& event) {
    if (!presentation_.editingScale_) return;
    if (event.GetId() == Rml::EventId::Keydown) {
        if (event.GetParameter<int>("key_identifier", 0) == static_cast<int>(Rml::Input::KI_ESCAPE)) {
            CloseTransient();
            event.GetCurrentElement()->Blur();
            event.StopPropagation();
        }
        return;
    }
    Rml::ElementFormControlInput* const input = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(event.GetCurrentElement());
    if (input == nullptr) return;
    presentation_.scaleDraft_ = input->GetValue();
    if (event.GetId() != Rml::EventId::Blur && !event.GetParameter<bool>("linebreak", false)) return;
    double value = 0.0;
    std::optional<double> scale;
    if (ParseDoubleText(presentation_.scaleDraft_, value)) scale = presentation_.CommitScale(value);
    else presentation_.CancelScaleEdit();
    pendingFocus_ = false;
    if (scale && refs_.has_value()) {
        GameSettingsAction action(*refs_);
        time_control_action::SetScale(action, *scale);
    }
    input->Blur();
    modelHandle_.DirtyVariable("timeControl");
}
}

#include "RmlUiGameSettingsView.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/ID.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/Traits.h>

#include "RmlUiNumberParse.h"
#include "../action/GameSettingsScreenModel.h"

namespace rs::app::rmlui {
void RmlUiGameSettingsView::OnTimeScaleCommit(Rml::Event& event) {
    if (!refs_.has_value()) return;
    Rml::ElementFormControl* const control = rmlui_dynamic_cast<Rml::ElementFormControl*>(event.GetCurrentElement());
    if (control == nullptr) return;

    const bool isBlur = event.GetId() == Rml::EventId::Blur;
    if (isBlur) {
        timeScaleFocused_ = false;
        if (suppressTimeScaleBlurCommit_) return; // Escの取り消し(確定しない)
    }
    // 入力途中(changeでlinebreakなし)は反映しない。変数には打った文字を写し、更新で消されないようにする。
    if (!isBlur && !event.GetParameter<bool>("linebreak", false)) {
        model_->timeScale = control->GetValue();
        return;
    }
    // Enter・blur: 数として読めたときだけ反映する。最後は今の値の表示に戻す。
    float value = 0.0f;
    if (ParseFloatText(control->GetValue(), value)) GameSettingsAction(*refs_).SetTimeScale(value);
    ResetTimeScaleText(*control);
}

void RmlUiGameSettingsView::OnTimeScaleKey(Rml::Event& event) {
    if (!refs_.has_value()) return;
    if (event.GetParameter<int>("key_identifier", 0) != static_cast<int>(Rml::Input::KI_ESCAPE)) return;
    Rml::Element* const element = event.GetCurrentElement();
    if (element == nullptr) return;

    // 入力を取り消して(今の値の表示に戻して)フォーカスを外す。blurでは確定しない。
    ResetTimeScaleText(*element);
    suppressTimeScaleBlurCommit_ = true;
    element->Blur();
    suppressTimeScaleBlurCommit_ = false;
    timeScaleFocused_ = false;
    event.StopPropagation();
}

void RmlUiGameSettingsView::ResetTimeScaleText(Rml::Element& element) {
    const std::string text = FormatTimeScale(GameSettingsAction(*refs_).ViewModel().timeScale);
    Rml::ElementFormControl* const control = rmlui_dynamic_cast<Rml::ElementFormControl*>(&element);
    if (control != nullptr && control->GetValue() != text) control->SetValue(text);
    model_->timeScale = text;
}

} // namespace rs::app::rmlui

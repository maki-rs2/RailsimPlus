#include "RmlUiSceneryYawStepView.h"

#include <format>
#include <string>

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/ID.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/Traits.h>

#include "RmlUiNumberParse.h"
#include "RmlUiUpdateVariable.h"
#include "../action/SceneryYawAction.h"

namespace rs::app::rmlui {

// データモデルへ出す値の入れ物(入力欄の文字)。
struct SceneryYawStepModel {
    Rml::String text;
};

namespace {

constexpr const char* kTextKey = "sceneryYawStepDeg";

} // namespace

RmlUiSceneryYawStepView::RmlUiSceneryYawStepView() : model_(std::make_unique<SceneryYawStepModel>()) {}
RmlUiSceneryYawStepView::~RmlUiSceneryYawStepView() = default;

void RmlUiSceneryYawStepView::RegisterModel(Rml::DataModelConstructor& constructor) {
    constructor.Bind(kTextKey, &model_->text);
    constructor.BindEventCallback("focus_scenery_yaw_step",
                                  [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
                                      focused_ = true;
                                  });
    constructor.BindEventCallback("commit_scenery_yaw_step",
                                  [this](Rml::DataModelHandle, Rml::Event& event, const Rml::VariantList&) {
                                      OnCommit(event);
                                  });
    constructor.BindEventCallback("key_scenery_yaw_step",
                                  [this](Rml::DataModelHandle, Rml::Event& event, const Rml::VariantList&) {
                                      OnKey(event);
                                  });
    modelHandle_ = constructor.GetModelHandle();
}

void RmlUiSceneryYawStepView::Refresh() {
    if (stepDeg_ == nullptr || !modelHandle_ || focused_) return; // フォーカス中の欄は上書きしない
    UpdateVariable(modelHandle_, kTextKey, model_->text, Rml::String(CurrentText()));
}

std::string RmlUiSceneryYawStepView::CurrentText() const {
    return std::format("{:.1f}", *stepDeg_);
}

void RmlUiSceneryYawStepView::OnCommit(Rml::Event& event) {
    if (stepDeg_ == nullptr) return;
    Rml::ElementFormControl* const control = rmlui_dynamic_cast<Rml::ElementFormControl*>(event.GetCurrentElement());
    if (control == nullptr) return;

    const bool isBlur = event.GetId() == Rml::EventId::Blur;
    if (isBlur) {
        focused_ = false;
        if (suppressBlurCommit_) return; // Escの取り消し(確定しない)
    }
    // 入力途中(changeでlinebreakなし)は反映しない。変数には打った文字を写し、更新で消されないようにする。
    if (!isBlur && !event.GetParameter<bool>("linebreak", false)) {
        model_->text = control->GetValue();
        return;
    }
    // Enter・blur: 数として読めたときだけ、範囲へ丸めて反映する。最後は今の値の表示に戻す。
    float value = 0.0f;
    if (ParseFloatText(control->GetValue(), value)) *stepDeg_ = SceneryYawAction::ClampStepDeg(value);
    ResetText(*control);
}

void RmlUiSceneryYawStepView::OnKey(Rml::Event& event) {
    if (stepDeg_ == nullptr) return;
    if (event.GetParameter<int>("key_identifier", 0) != static_cast<int>(Rml::Input::KI_ESCAPE)) return;
    Rml::Element* const element = event.GetCurrentElement();
    if (element == nullptr) return;

    // 入力を取り消して(今の値の表示に戻して)フォーカスを外す。blurでは確定しない。
    ResetText(*element);
    suppressBlurCommit_ = true;
    element->Blur();
    suppressBlurCommit_ = false;
    focused_ = false;
    event.StopPropagation();
}

void RmlUiSceneryYawStepView::ResetText(Rml::Element& element) {
    const std::string text = CurrentText();
    Rml::ElementFormControl* const control = rmlui_dynamic_cast<Rml::ElementFormControl*>(&element);
    if (control != nullptr && control->GetValue() != text) control->SetValue(text);
    model_->text = text;
}

} // namespace rs::app::rmlui

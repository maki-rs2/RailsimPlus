#include "RmlUiRenderSettingsView.h"

#include <string>

#include <RmlUi/Core/DataStructHandle.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Types.h>

#include "RmlUiDataEventArgs.h"
#include "RmlUiNumberParse.h"
#include "RmlUiUpdateVariable.h"
#include "RmlUiUpdatePhase.h"
#include "../action/RenderSettingsRows.h"
#include "../action/GameSettingsScreenModel.h"
#include "../action/RenderQualityPreset.h"

namespace rs::app::rmlui {

// データモデルへ出す値の入れ物(定義はここだけ)。
struct RenderSettingsModel {
    Rml::Vector<RenderSettingRowView> rows;
};

namespace {

constexpr const char* kRowsKey = "renderSettingRows";

} // namespace

RmlUiRenderSettingsView::RmlUiRenderSettingsView(ToastQueue& queue)
    : model_(std::make_unique<RenderSettingsModel>()), queue_(queue) {}
RmlUiRenderSettingsView::~RmlUiRenderSettingsView() = default;

void RmlUiRenderSettingsView::Attach(const RenderSettingsRefs& refs, const Localization& localization) {
    refs_.emplace(refs);
    localization_ = &localization;
}

void RmlUiRenderSettingsView::RegisterModel(Rml::DataModelConstructor& constructor) {
    if (Rml::StructHandle<RenderSettingRowView> handle = constructor.RegisterStruct<RenderSettingRowView>()) {
        handle.RegisterMember("key", &RenderSettingRowView::key);
        handle.RegisterMember("kind", &RenderSettingRowView::kind);
        handle.RegisterMember("tab", &RenderSettingRowView::tab);
        handle.RegisterMember("section", &RenderSettingRowView::section);
        handle.RegisterMember("label", &RenderSettingRowView::label);
        handle.RegisterMember("note", &RenderSettingRowView::note);
        handle.RegisterMember("disabled", &RenderSettingRowView::disabled);
        handle.RegisterMember("checked", &RenderSettingRowView::checked);
        handle.RegisterMember("valueText", &RenderSettingRowView::valueText);
        handle.RegisterMember("options", &RenderSettingRowView::options);
        handle.RegisterMember("sliderPosition", &RenderSettingRowView::sliderPosition);
        handle.RegisterMember("valueLabel", &RenderSettingRowView::valueLabel);
    }
    constructor.RegisterArray<Rml::Vector<RenderSettingRowView>>();
    constructor.Bind(kRowsKey, &model_->rows);
    constructor.Bind("gsQualityPreset", &presentation_.preset_);
    constructor.Bind("gsQualityLabel", &presentation_.label_);
    constructor.Bind("gsQualityCustom", &presentation_.customLabel_);
    constructor.Bind("gsQualityNote", &presentation_.note_);
    constructor.Bind("gsQualityChoices", &presentation_.choices_);
    constructor.BindEventCallback("gs_quality_preset", [this](Rml::DataModelHandle, Rml::Event& event,
        const Rml::VariantList&) {
        if (IsInsideContextUpdate()) return;
        if (!refs_.has_value()) return;
        const std::string value = event.GetParameter<std::string>("value", std::string(kCustomQualityPresetKey));
        if (value == presentation_.preset_) return;
        RenderSettingsAction action(*refs_);
        if (ApplyRenderQualityPreset(action, action.ViewModel(), RenderQualityPresets::Embedded(), value)) {
            UpdateVariable(modelHandle_, "gsQualityPreset", presentation_.preset_, value);
        } else {
            // 「カスタム」は選んで適用するものではない。選択を今の設定の判定へ戻す。
            modelHandle_.DirtyVariable("gsQualityPreset");
        }
    });

    constructor.BindEventCallback("rs_toggle",
                                  [this](Rml::DataModelHandle, Rml::Event& event, const Rml::VariantList& a) {
                                      OnToggle(event, a);
                                  });
    constructor.BindEventCallback("rs_choice",
                                  [this](Rml::DataModelHandle, Rml::Event& event, const Rml::VariantList& a) {
                                      OnChoice(event, a);
                                  });
    constructor.BindEventCallback("rs_slider",
                                  [this](Rml::DataModelHandle, Rml::Event& event, const Rml::VariantList& a) {
                                      OnSlider(event, a);
                                  });
    modelHandle_ = constructor.GetModelHandle();
}

void RmlUiRenderSettingsView::Tick() {
    if (!refs_.has_value() || !modelHandle_) return;
    const int language = static_cast<int>(localization_->CurrentLanguage());
    if (shownLanguage_ != language) {
        shownLanguage_ = language;
        presentation_ = BuildRenderSettingsPresentation(presentation_, *localization_);
        modelHandle_.DirtyVariable("gsQualityLabel");
        modelHandle_.DirtyVariable("gsQualityCustom");
        modelHandle_.DirtyVariable("gsQualityNote");
        modelHandle_.DirtyVariable("gsQualityChoices");
    }
    const RenderSettingsViewModel viewModel = RenderSettingsAction(*refs_).ViewModel();
    Rml::Vector<RenderSettingRowView> rows = BuildRenderSettingRowViews(
        viewModel, *localization_, RenderSettingsAction::RenderDistanceMin(), RenderSettingsAction::RenderDistanceMax());
    OrderRenderTabRows(rows);
    UpdateVariable(modelHandle_, kRowsKey, model_->rows, rows);
    // プリセットの選択表示は、設定の値がどのプリセットと一致するかから毎回決める(個別の項目を変えると外れる)。
    UpdateVariable(modelHandle_, "gsQualityPreset", presentation_.preset_,
                   std::string(MatchRenderQualityPreset(RenderQualityPresets::Embedded(), viewModel)));
}

void RmlUiRenderSettingsView::OnToggle(Rml::Event& event, const Rml::VariantList& arguments) {
    if (!refs_.has_value()) return;
    const Rml::Element* const element = event.GetCurrentElement();
    const RenderSettingRow* const row = FindRenderSettingRow(FirstEventArgument<Rml::String>(arguments, Rml::String()));
    if (element == nullptr || row == nullptr || row->kind != RenderSettingKind::Toggle) return;
    RenderSettingsAction action(*refs_);
    const bool enabled = element->HasAttribute("checked");
    // 画面の値を書き戻しただけのchangeでは書き込まない。
    if ((row->get(action.ViewModel()) != 0) == enabled) return;
    ApplyRenderSettingToggle(row->key, enabled, action);
}

void RmlUiRenderSettingsView::OnChoice(Rml::Event& event, const Rml::VariantList& arguments) {
    if (!refs_.has_value()) return;
    const RenderSettingRow* const row = FindRenderSettingRow(FirstEventArgument<Rml::String>(arguments, Rml::String()));
    int index = 0;
    if (row == nullptr || row->kind != RenderSettingKind::Choice ||
        !ParseIntText(event.GetParameter<Rml::String>("value", Rml::String()), index) || index < 0) {
        return;
    }
    RenderSettingsAction action(*refs_);
    if (RenderChoiceSelectedIndex(*row, action.ViewModel()) == static_cast<std::size_t>(index)) return;
    ApplyRenderSettingChoice(row->key, static_cast<std::size_t>(index), action);
}

void RmlUiRenderSettingsView::OnSlider(Rml::Event& event, const Rml::VariantList& arguments) {
    if (!refs_.has_value()) return;
    const RenderSettingRow* const row = FindRenderSettingRow(FirstEventArgument<Rml::String>(arguments, Rml::String()));
    const float value = event.GetParameter<float>("value", -1.0f);
    if (row == nullptr || row->kind != RenderSettingKind::LogSlider || value < 0.0f) return;
    const float minimum = RenderSettingsAction::RenderDistanceMin();
    const float maximum = RenderSettingsAction::RenderDistanceMax();
    RenderSettingsAction action(*refs_);
    const int position = static_cast<int>(value);
    if (LogValueToSliderPosition(row->getFloat(action.ViewModel()), minimum, maximum) == position) return;
    ApplyRenderSettingSlider(row->key, position, minimum, maximum, action);
}

} // namespace rs::app::rmlui

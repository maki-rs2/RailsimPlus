#include "RmlUiLoadingView.h"

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

#include "RmlUiUpdateVariable.h"

namespace rs::app::rmlui {

// データモデルへ出す値の入れ物(定義はここだけ)。
struct LoadingModel {
    bool visible = false;
    Rml::String label;
    float fraction = 0.0f;
    Rml::String count;
};

namespace {
constexpr const char* kVisibleKey = "loadingVisible";
constexpr const char* kLabelKey = "loadingLabel";
constexpr const char* kFractionKey = "loadingFraction";
constexpr const char* kCountKey = "loadingCount";
} // namespace

RmlUiLoadingView::RmlUiLoadingView() : model_(std::make_unique<LoadingModel>()) {}
RmlUiLoadingView::~RmlUiLoadingView() = default;

void RmlUiLoadingView::RegisterModel(Rml::DataModelConstructor& constructor) {
    constructor.Bind(kVisibleKey, &model_->visible);
    constructor.Bind(kLabelKey, &model_->label);
    constructor.Bind(kFractionKey, &model_->fraction);
    constructor.Bind(kCountKey, &model_->count);
    modelHandle_ = constructor.GetModelHandle();
}

void RmlUiLoadingView::Set(bool visible, const std::string& label, float fraction, const std::string& count) {
    if (!modelHandle_) return;
    UpdateVariable(modelHandle_, kVisibleKey, model_->visible, visible);
    UpdateVariable(modelHandle_, kLabelKey, model_->label, label);
    UpdateVariable(modelHandle_, kFractionKey, model_->fraction, fraction);
    UpdateVariable(modelHandle_, kCountKey, model_->count, count);
}

} // namespace rs::app::rmlui

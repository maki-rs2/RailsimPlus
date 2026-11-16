#include "RmlUiCameraPhotoPreview.h"

#include <algorithm>
#include <charconv>
#include <utility>

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/DataStructHandle.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Property.h>
#include <RmlUi/Core/ID.h>

namespace rs::app::rmlui {
namespace {
double ReadSeconds(Rml::Element& element, const char* name) {
    const Rml::Property* const property = element.GetProperty(name);
    if (property == nullptr) return 0.0;
    const std::string value = property->Get<Rml::String>();
    double seconds = 0.0;
    std::from_chars(value.data(), value.data() + value.size(), seconds);
    return seconds;
}

void ResolveTransition(Rml::Context& context, const char* selector) {
    Rml::Element* const element = context.GetRootElement()->QuerySelector(selector);
    if (element == nullptr) return;
    // クラス切替の遷移処理はvar()を展開しないため、RCSSから解決した値を先に渡す。
    element->RemoveProperty(Rml::PropertyId::Transition);
    if (const Rml::Property* const property = element->GetProperty(Rml::PropertyId::Transition)) {
        const Rml::Property resolved = *property;
        element->SetProperty(Rml::PropertyId::Transition, resolved);
    }
}
}

RmlUiCameraPhotoPreview::~RmlUiCameraPhotoPreview() {
    if (!data_.source_.empty()) Rml::ReleaseTexture(data_.source_);
}

void RmlUiCameraPhotoPreview::RegisterModel(Rml::DataModelConstructor& constructor, Rml::Context& context) {
    context_ = &context;
    Rml::StructHandle<CameraPhotoPreviewData> photo = constructor.RegisterStruct<CameraPhotoPreviewData>();
    photo.RegisterMember("source", &CameraPhotoPreviewData::source_);
    photo.RegisterMember("phase", &CameraPhotoPreviewData::phase_);
    photo.RegisterMember("origin", &CameraPhotoPreviewData::origin_);
    photo.RegisterMember("target", &CameraPhotoPreviewData::target_);
    constructor.Bind("cameraPhoto", &data_);
    modelHandle_ = constructor.GetModelHandle();
}

void RmlUiCameraPhotoPreview::Clear() {
    // データモデルの更新より先に描画されても、解放した画像を読み直さないようにする。
    if (context_ != nullptr) {
        if (Rml::Element* const image = context_->GetRootElement()->QuerySelector(".camera-photo-preview")) {
            image->SetAttribute("src", Rml::String());
            image->SetProperty("transition", "none");
        }
        if (Rml::Element* const flash = context_->GetRootElement()->QuerySelector(".camera-photo-flash")) {
            flash->SetProperty("transition", "none");
        }
    }
    if (!data_.source_.empty()) Rml::ReleaseTexture(data_.source_);
    preview_.Clear();
    pendingPath_.clear();
    data_ = {};
    if (modelHandle_) modelHandle_.DirtyVariable("cameraPhoto");
}

void RmlUiCameraPhotoPreview::Show(std::string path, const workspace::CameraFinderRect& finder) {
    Clear();
    pendingPath_ = std::move(path);
    origin_ = finder;
}

void RmlUiCameraPhotoPreview::Refresh(Rml::Context& context, double deltaSeconds) {
    const workspace::CameraPhotoPhase previous = preview_.Phase();
    if (!pendingPath_.empty()) {
        Rml::Element* const target = context.GetRootElement()->QuerySelector(".camera-photo-target");
        if (target == nullptr) return;
        const Rml::Vector2f size = target->GetBox().GetSize(Rml::BoxArea::Content);
        const Rml::Vector2f offset = target->GetAbsoluteOffset(Rml::BoxArea::Content);
        if (size.x <= 0.0f || size.y <= 0.0f) return;
        const double scale = std::min(size.x / origin_.width_, size.y / origin_.height_);
        const double width = origin_.width_ * scale;
        const double height = origin_.height_ * scale;
        target_ = {offset.x + size.x - width, offset.y + size.y - height, width, height};
        ResolveTransition(context, ".camera-photo-preview");
        preview_.Start(std::exchange(pendingPath_, {}), {
            ReadSeconds(*target, "--camera-photo-flash-time"), ReadSeconds(*target, "--camera-photo-flight-time"),
            ReadSeconds(*target, "--camera-photo-hold-time"), ReadSeconds(*target, "--camera-photo-fade-time")});
    } else {
        // 白フラッシュの立ち上がりは即時にし、消えるときだけ遷移させる。
        if (previous == workspace::CameraPhotoPhase::Started) ResolveTransition(context, ".camera-photo-flash");
        preview_.Advance(deltaSeconds);
    }
    const workspace::CameraPhotoPhase phase = preview_.Phase();
    if (phase == previous) return;
    if (phase == workspace::CameraPhotoPhase::Hidden) {
        Clear();
        return;
    }
    data_.source_ = preview_.Source();
    data_.phase_ = static_cast<int>(phase);
    data_.origin_ = origin_;
    data_.target_ = target_;
    modelHandle_.DirtyVariable("cameraPhoto");
}
}

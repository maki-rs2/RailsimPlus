#include "RmlUiCameraScreenView.h"
#include "../settings/UserSettings.h"
#include "../camera/CameraControl.h"
#include "../../core/Math.h"

#include <optional>

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataStructHandle.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/ID.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/Traits.h>

#include "RmlUiDataEventArgs.h"
#include "RmlUiUpdatePhase.h"
#include "../action/CameraScreenAction.h"
#include "../camera/PhotoCamera.h"
#include "../action/ToastQueue.h"
#include "../locale/Localization.h"
#include "../workspace/CameraScreenPlaceholder.h"

namespace rs::app::rmlui {
using namespace workspace;

RmlUiCameraScreenView::RmlUiCameraScreenView(ToastQueue& queue)
    : queue_(queue), presentation_(MakeCameraScreenPlaceholder()) {}

void RmlUiCameraScreenView::RegisterModel(Rml::DataModelConstructor& constructor, Rml::Context& context) {
    context_ = &context;
    Rml::StructHandle<CameraScreenSegment> segment = constructor.RegisterStruct<CameraScreenSegment>();
    segment.RegisterMember("index", &CameraScreenSegment::index_);
    segment.RegisterMember("on", &CameraScreenSegment::on_);
    constructor.RegisterArray<std::vector<CameraScreenSegment>>();
    Rml::StructHandle<CameraScreenDigit> digit = constructor.RegisterStruct<CameraScreenDigit>();
    digit.RegisterMember("narrow", &CameraScreenDigit::narrow_);
    digit.RegisterMember("segments", &CameraScreenDigit::segments_);
    constructor.RegisterArray<std::vector<CameraScreenDigit>>();
    Rml::StructHandle<CameraScreenField> field = constructor.RegisterStruct<CameraScreenField>();
    field.RegisterMember("label", &CameraScreenField::label_);
    field.RegisterMember("text", &CameraScreenField::text_);
    field.RegisterMember("draft", &CameraScreenField::draft_);
    field.RegisterMember("placement", &CameraScreenField::placement_);
    field.RegisterMember("unit", &CameraScreenField::unit_);
    field.RegisterMember("digits", &CameraScreenField::digits_);
    constructor.RegisterArray<std::vector<CameraScreenField>>();
    Rml::StructHandle<CameraScreenResolution> resolution = constructor.RegisterStruct<CameraScreenResolution>();
    resolution.RegisterMember("label", &CameraScreenResolution::label_);
    constructor.RegisterArray<std::vector<CameraScreenResolution>>();
    Rml::StructHandle<CameraFinderRect> overlay = constructor.RegisterStruct<CameraFinderRect>();
    overlay.RegisterMember("left", &CameraFinderRect::left_);
    overlay.RegisterMember("top", &CameraFinderRect::top_);
    overlay.RegisterMember("width", &CameraFinderRect::width_);
    overlay.RegisterMember("height", &CameraFinderRect::height_);
    constructor.RegisterArray<std::vector<CameraFinderRect>>();
    photoPreview_.RegisterModel(constructor, context);
    Rml::StructHandle<CameraScreenPresentation> screen = constructor.RegisterStruct<CameraScreenPresentation>();
    screen.RegisterMember("active", &CameraScreenPresentation::active_);
    screen.RegisterMember("fields", &CameraScreenPresentation::fields_);
    screen.RegisterMember("bookmarks", &CameraScreenPresentation::bookmarks_);
    screen.RegisterMember("resolutions", &CameraScreenPresentation::resolutions_);
    screen.RegisterMember("resolution", &CameraScreenPresentation::resolution_);
    screen.RegisterMember("finder", &CameraScreenPresentation::finder_);
    screen.RegisterMember("masks", &CameraScreenPresentation::masks_);
    screen.RegisterMember("focusShown", &CameraScreenPresentation::focusShown_);
    screen.RegisterMember("finderGrid", &CameraScreenPresentation::finderGrid_);
    screen.RegisterMember("finderHorizon", &CameraScreenPresentation::finderHorizon_);
    screen.RegisterMember("rollDegrees", &CameraScreenPresentation::rollDegrees_);
    screen.RegisterMember("rolling", &CameraScreenPresentation::rolling_);
    screen.RegisterMember("focusX", &CameraScreenPresentation::focusX_);
    screen.RegisterMember("focusY", &CameraScreenPresentation::focusY_);
    constructor.Bind("camera", &presentation_);
    constructor.BindEventCallback("camera_action", [this](Rml::DataModelHandle, Rml::Event& event,
        const Rml::VariantList& arguments) { OnAction(event, arguments); });
    constructor.BindEventCallback("camera_field", [this](Rml::DataModelHandle, Rml::Event& event,
        const Rml::VariantList& arguments) { OnField(event, EventArgumentAt<int>(arguments, 0, -1)); });
    constructor.BindEventCallback("camera_bookmark_edit", [this](Rml::DataModelHandle, Rml::Event& event,
        const Rml::VariantList&) { OnBookmarkEdit(event); });
    modelHandle_ = constructor.GetModelHandle();
}

bool RmlUiCameraScreenView::HandleViewportClick(float x, float y) {
    if (!presentation_.active_ || !presentation_.FocusAt(x, y)) return false;
    const workspace::CameraFinderRect& finder = presentation_.finder_;
    presentation_.focusProbe_ = std::make_pair((x - finder.left_) / finder.width_, (y - finder.top_) / finder.height_);
    return true;
}

std::optional<std::pair<double, double>> RmlUiCameraScreenView::TakeFocusProbe() {
    return std::exchange(presentation_.focusProbe_, std::nullopt);
}

bool RmlUiCameraScreenView::ApplyFocusDepth(double depthM) {
    if (!presentation_.active_) return false;
    const bool applied = camera_screen_action::FocusAtDepth(presentation_, depthM);
    modelHandle_.DirtyVariable("camera");
    return applied;
}

std::optional<workspace::CameraFinderRect> RmlUiCameraScreenView::ActiveFinder() const {
    if (!presentation_.active_ || presentation_.finder_.width_ <= 0.0 || presentation_.finder_.height_ <= 0.0) {
        return std::nullopt;
    }
    return presentation_.finder_;
}

bool RmlUiCameraScreenView::SetFieldText(CameraScreenParameter parameter, std::string_view text) {
    const std::size_t index = static_cast<std::size_t>(parameter);
    if (index >= presentation_.fields_.size() || !presentation_.fields_[index].SetText(text)) return false;
    modelHandle_.DirtyVariable("camera");
    return true;
}

void RmlUiCameraScreenView::SetAutoIso(std::optional<double> iso) {
    const std::size_t index = static_cast<std::size_t>(CameraScreenParameter::Iso);
    if (index >= presentation_.fields_.size()) return;
    CameraScreenField& field = presentation_.fields_[index];
    // 7セグには、段のうち最も近いISOを出す。決まった値が無いときは基準の値。
    const std::string shown = iso ? NearestNumericPhotoStep(CameraScreenParameter::Iso, field.steps_, *iso) : std::string();
    if (field.SetAutoDisplay(shown)) modelHandle_.DirtyVariable("camera");
}

bool RmlUiCameraScreenView::TakePhotoByKey() {
    if (!presentation_.active_) return false;
    camera_screen_action::TakePhoto(presentation_);
    return true;
}

std::optional<CameraFinderRect> RmlUiCameraScreenView::BeginPhotoCapture(bool otherCaptureRequested) {
    if (context_ == nullptr || !presentation_.BeginPhotoCapture(otherCaptureRequested)) return std::nullopt;
    photoPreview_.Clear();
    photoFinder_ = ActiveFinder().value_or(CameraFinderRect{});
    const Rml::Vector2i dimensions = context_->GetDimensions();
    return NormalizeCameraFinder(photoFinder_, dimensions.x, dimensions.y);
}

void RmlUiCameraScreenView::CompletePhoto(const std::string& path, const std::string& errorKey, const Localization& localization) {
    presentation_.CompletePhoto();
    if (!errorKey.empty()) queue_.Post(ToastSeverity::Error, localization.Text(errorKey));
    else if (presentation_.active_) photoPreview_.Show(path, photoFinder_);
}

void RmlUiCameraScreenView::SetActive(bool active) {
    if (presentation_.active_ == active) return;
    presentation_.active_ = active;
    if (!active) {
        photoPreview_.Clear();
        presentation_.CompletePhoto();
    }
    CloseTransient();
    modelHandle_.DirtyVariable("camera");
}

bool RmlUiCameraScreenView::CloseTransient() {
    bool closed = false;
    if (editingField_ >= 0) {
        CameraScreenField& field = presentation_.fields_[editingField_];
        field.draft_ = field.text_;
        editingField_ = -1;
        closed = true;
    }
    if (presentation_.bookmarks_.CloseTransient()) closed = true;
    pendingFocus_ = false;
    if (closed) modelHandle_.DirtyVariable("camera");
    return closed;
}

void RmlUiCameraScreenView::RefreshLayout() {
    Rml::Element* const space = context_->GetRootElement()->QuerySelector(".camera-finder-space");
    if (space == nullptr || !space->IsVisible(true)) return;
    const Rml::Vector2f offset = space->GetAbsoluteOffset(Rml::BoxArea::Content);
    const Rml::Vector2f size = space->GetBox().GetSize(Rml::BoxArea::Content);
    const Rml::Vector2i screen = context_->GetDimensions();
    const CameraFinderRect previous = presentation_.finder_;
    presentation_.Layout(screen.x, screen.y, {offset.x, offset.y, size.x, size.y});
    if (previous != presentation_.finder_) modelHandle_.DirtyVariable("camera");
}

void RmlUiCameraScreenView::Refresh(double deltaSeconds) {
    if (!presentation_.active_ || context_ == nullptr) return;
    RefreshLayout();
    photoPreview_.Refresh(*context_, deltaSeconds);
    if (camera_ != nullptr) {
        const double rollDegrees = static_cast<double>(camera_->fly.roll) * rs::core::kRadToDeg;
        if (presentation_.rollDegrees_ != rollDegrees) {
            presentation_.rollDegrees_ = rollDegrees;
            modelHandle_.DirtyVariable("camera");
        }
        if (presentation_.rolling_ != camera_->IsRolling()) {
            presentation_.rolling_ = camera_->IsRolling();
            modelHandle_.DirtyVariable("camera");
        }
    }
    if (!pendingFocus_) return;
    Rml::Element* const element = context_->GetRootElement()->QuerySelector(".camera-bookmark-input.is-editing");
    Rml::ElementFormControlInput* const input = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(element);
    if (input == nullptr || !input->IsVisible(true)) return;
    pendingFocus_ = false;
    input->SetValue(presentation_.bookmarks_.rows_[presentation_.bookmarks_.editingRow_].draft_);
    input->Focus();
    input->Select();
}

void RmlUiCameraScreenView::ApplyField(int index, std::string_view value) {
    // 撮影カメラの状態は入力欄の値そのもの。変換できない文字列は受け付けない。
    if (!IsCameraScreenAutoText(value) && !ParsePhotoValue(static_cast<CameraScreenParameter>(index), value)) return;
    presentation_.fields_[index].SetText(value);
}

void RmlUiCameraScreenView::OnAction(Rml::Event& event, const Rml::VariantList& arguments) {
    if (IsInsideContextUpdate() || !presentation_.active_) return;
    event.StopPropagation();
    const std::string kind = EventArgumentAt<std::string>(arguments, 0, "");
    const int index = EventArgumentAt<int>(arguments, 1, -1);
    ScenePanelPresentation& bookmarks = presentation_.bookmarks_;
    using namespace camera_screen_action;
    if (kind == "wheel" && index >= 0 && static_cast<std::size_t>(index) < presentation_.fields_.size()) {
        CameraScreenField& field = presentation_.fields_[index];
        field.Wheel(event.GetParameter<double>("wheel_delta_y", 0.0));
    } else if (kind == "step" && index >= 0 && static_cast<std::size_t>(index) < presentation_.fields_.size()) {
        // 矢印ボタンはホイールと同じ段を1つ動かす(右=上げる)。左のパネルの欄も同じ値を持つので一緒に変わる。
        CameraScreenField& field = presentation_.fields_[index];
        const int direction = EventArgumentAt<int>(arguments, 2, 0);
        field.Wheel(direction > 0 ? -1.0 : 1.0);
    } else if (kind == "resolution") {
        const std::string value = event.GetParameter<Rml::String>("value", std::to_string(presentation_.resolution_));
        if (presentation_.SetResolution(value)) SetResolution(queue_, value);
    } else if (kind == "photo") TakePhotoByKey();
    else if (kind == "add") AddBookmark(queue_);
    else if (kind == "goto" && bookmarks.ValidRow(index)) GoToBookmark(queue_, index);
    else if (kind == "delete" && bookmarks.ValidRow(index)) DeleteBookmark(queue_, index);
    else if (kind == "rename" && bookmarks.BeginRename(index)) pendingFocus_ = true;
    else if ((kind == "grid" || kind == "horizon") && settings_ != nullptr) {
        // ファインダーの線の切り替えは表示の設定なので、すぐ保存する。
        bool& value = kind == "grid" ? settings_->cameraFinderGrid : settings_->cameraFinderHorizon;
        value = !value;
        presentation_.finderGrid_ = settings_->cameraFinderGrid;
        presentation_.finderHorizon_ = settings_->cameraFinderHorizon;
        SaveUserSettingsOrWarn(*settings_, "Failed to save camera finder guides.");
    }
    modelHandle_.DirtyVariable("camera");
}

void RmlUiCameraScreenView::OnField(Rml::Event& event, int index) {
    if (IsInsideContextUpdate() || !presentation_.active_ || index < 0 ||
        static_cast<std::size_t>(index) >= presentation_.fields_.size()) return;
    Rml::ElementFormControlInput* const input = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(event.GetCurrentElement());
    if (input == nullptr) return;
    CameraScreenField& field = presentation_.fields_[index];
    if (event.GetId() == Rml::EventId::Focus) {
        editingField_ = index;
        return;
    }
    if (editingField_ != index) return;
    if (event.GetId() == Rml::EventId::Keydown) {
        if (event.GetParameter<int>("key_identifier", 0) == static_cast<int>(Rml::Input::KI_ESCAPE)) {
            CloseTransient();
            input->SetValue(field.draft_);
            input->Blur();
            event.StopPropagation();
        }
        return;
    }
    field.draft_ = input->GetValue();
    if (event.GetId() != Rml::EventId::Blur && !event.GetParameter<bool>("linebreak", false)) return;
    editingField_ = -1;
    if (!field.draft_.empty()) ApplyField(index, field.draft_);
    field.draft_ = field.text_;
    input->SetValue(field.draft_);
    input->Blur();
    modelHandle_.DirtyVariable("camera");
}

void RmlUiCameraScreenView::OnBookmarkEdit(Rml::Event& event) {
    ScenePanelPresentation& bookmarks = presentation_.bookmarks_;
    if (IsInsideContextUpdate() || bookmarks.editingRow_ < 0) return;
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
    bookmarks.rows_[bookmarks.editingRow_].draft_ = input->GetValue();
    if (event.GetId() != Rml::EventId::Blur && !event.GetParameter<bool>("linebreak", false)) return;
    const std::optional<SceneRenameRequest> request = bookmarks.CommitRename();
    pendingFocus_ = false;
    if (request) camera_screen_action::RenameBookmark(queue_, request->row_, request->name_);
    input->Blur();
    modelHandle_.DirtyVariable("camera");
}

void RmlUiCameraScreenView::AttachCamera(const CameraController& camera) {
    camera_ = &camera;
}

void RmlUiCameraScreenView::AttachSettings(UserSettingsData& settings) {
    settings_ = &settings;
    presentation_.finderGrid_ = settings.cameraFinderGrid;
    presentation_.finderHorizon_ = settings.cameraFinderHorizon;
    if (modelHandle_) modelHandle_.DirtyVariable("camera");
}
}

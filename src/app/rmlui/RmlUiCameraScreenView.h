#pragma once

#include <optional>
#include <utility>

#include <RmlUi/Core/DataModelHandle.h>
#include "../workspace/CameraScreenPresentation.h"
#include "RmlUiCameraPhotoPreview.h"

namespace Rml { class Context; class Event; }
namespace rs::app { class CameraController; class Localization; class ToastQueue; struct UserSettingsData; }

namespace rs::app::rmlui {
class RmlUiCameraScreenView {
public:
    explicit RmlUiCameraScreenView(ToastQueue& queue);
    void RegisterModel(Rml::DataModelConstructor& constructor, Rml::Context& context);
    void SetActive(bool active);
    // ファインダーの線の切り替えを読み書きする設定(保存もここから行う)。
    void AttachSettings(UserSettingsData& settings);
    void AttachCamera(const CameraController& camera);
    void Refresh(double deltaSeconds);
    std::optional<workspace::CameraFinderRect> BeginPhotoCapture(bool otherCaptureRequested);
    void CompletePhoto(const std::string& path, const std::string& errorKey, const Localization& localization);
    bool CloseTransient();
    // 撮影モードのときだけ、Enterキーでも撮影する(撮影ボタンと同じ操作)。撮影したらtrue。
    bool TakePhotoByKey();
    // カメラ画面を開いていて、ファインダーの枠が決まっているとき、その枠(画面のピクセル座標)。
    std::optional<workspace::CameraFinderRect> ActiveFinder() const;
    // 3D画面(UIの外)のクリック。カメラ画面でファインダーの中なら、フォーカスの要求を出してtrue(印はすぐ置く)。
    bool HandleViewportClick(float x, float y);
    // クリックでフォーカスの要求(ファインダー内の割合)を1回だけ取り出す。奥行きが読めたら ApplyFocusDepth へ渡す。
    std::optional<std::pair<double, double>> TakeFocusProbe();
    // フォーカス欄へ入れたら true。
    bool ApplyFocusDepth(double depthM);
    const workspace::CameraScreenPresentation& Presentation() const { return presentation_; }
    // 入力欄の値をコードから書く(画面にも反映する)。入力欄の幅に収まらないときは変えずにfalse。
    bool SetFieldText(workspace::CameraScreenParameter parameter, std::string_view text);
    // ISOオート中に7セグへ出す、決まったISO。nullopt なら基準の値を出す。
    void SetAutoIso(std::optional<double> iso);

private:
    void OnAction(Rml::Event& event, const Rml::VariantList& arguments);
    void OnField(Rml::Event& event, int index);
    void OnBookmarkEdit(Rml::Event& event);
    void ApplyField(int index, std::string_view value);
    void RefreshLayout();
    ToastQueue& queue_;
    workspace::CameraScreenPresentation presentation_;
    RmlUiCameraPhotoPreview photoPreview_;
    workspace::CameraFinderRect photoFinder_;
    Rml::DataModelHandle modelHandle_;
    Rml::Context* context_ = nullptr;
    UserSettingsData* settings_ = nullptr;
    const CameraController* camera_ = nullptr;
    int editingField_ = -1;
    bool pendingFocus_ = false;
};
}

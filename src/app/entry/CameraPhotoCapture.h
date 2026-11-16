#pragma once

#include <optional>
#include "../workspace/CameraFinder.h"

namespace rs::app {
class App;
class Localization;
namespace rmlui { class RmlUiCameraScreenView; }

class CameraPhotoCapture {
public:
    // 撮影を描画側へ頼んだフレームだけ true。
    bool BeforeRender(App& app, rmlui::RmlUiCameraScreenView& view, bool otherCaptureRequested);
    void AfterRender(App& app, rmlui::RmlUiCameraScreenView& view, const Localization& localization);
private:
    std::optional<workspace::CameraFinderRect> finder_;
};

// ファインダーのクリックでフォーカス。クリックの点の奥行きを描画側へ頼み、読めたらフォーカス欄へ入れる。
class CameraFocusProbe {
public:
    void BeforeRender(App& app, rmlui::RmlUiCameraScreenView& view);
    // 奥行きが読めてフォーカス欄へ入れたフレームだけ true。
    bool AfterRender(App& app, rmlui::RmlUiCameraScreenView& view);
};
}

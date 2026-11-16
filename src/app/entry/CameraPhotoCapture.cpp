#include "CameraPhotoCapture.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "ScreenshotCapture.h"
#include "../gpu/App.h"
#include "../rmlui/RmlUiCameraScreenView.h"

namespace rs::app {
bool CameraPhotoCapture::BeforeRender(App& app, rmlui::RmlUiCameraScreenView& view, bool otherCaptureRequested) {
    finder_ = view.BeginPhotoCapture(otherCaptureRequested);
    if (!finder_) return false;
    app.RequestBackBufferCapture(BackBufferCaptureStage::SceneOnly);
    return true;
}

void CameraFocusProbe::BeforeRender(App& app, rmlui::RmlUiCameraScreenView& view) {
    if (const std::optional<std::pair<double, double>> point = view.TakeFocusProbe()) {
        app.RequestDepthProbe(point->first, point->second);
    }
}

bool CameraFocusProbe::AfterRender(App& app, rmlui::RmlUiCameraScreenView& view) {
    const std::optional<double> depthM = app.PollDepthProbe();
    return depthM && view.ApplyFocusDepth(*depthM);
}

void CameraPhotoCapture::AfterRender(App& app, rmlui::RmlUiCameraScreenView& view, const Localization& localization) {
    const std::optional<workspace::CameraFinderRect> finder = std::exchange(finder_, std::nullopt);
    if (!finder) return;
    std::vector<std::uint8_t> pixels;
    UINT width = 0, height = 0, rowPitch = 0;
    if (!app.TakeCapturedBackBuffer(pixels, width, height, rowPitch) ||
        rowPitch < static_cast<std::size_t>(width) * 4 || pixels.size() < static_cast<std::size_t>(rowPitch) * height) {
        view.CompletePhoto({}, "camera.photo.readback_failed", localization);
        return;
    }
    const workspace::CameraPhotoCrop crop = workspace::CalculateCameraPhotoCrop(*finder, width, height);
    if (crop.width == 0 || crop.height == 0) {
        view.CompletePhoto({}, "camera.photo.readback_failed", localization);
        return;
    }
    const std::uint32_t croppedPitch = crop.width * 4;
    std::vector<std::uint8_t> cropped(static_cast<std::size_t>(croppedPitch) * crop.height);
    for (std::uint32_t row = 0; row < crop.height; ++row) {
        const std::size_t source = static_cast<std::size_t>(crop.top + row) * rowPitch + static_cast<std::size_t>(crop.left) * 4;
        std::copy_n(pixels.data() + source, croppedPitch, cropped.data() + static_cast<std::size_t>(row) * croppedPitch);
    }
    const ScreenshotSaveResult result = SaveScreenshotPng(cropped, crop.width, crop.height, croppedPitch);
    view.CompletePhoto(result.pathUtf8, result.errorKey, localization);
}
}

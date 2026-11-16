//
// AppRuntimeFrameFov.cpp - フレームの実効の視野角の決定(ファインダーの出入りの扱いを含む)
//
#include "AppRuntimeFrameInternal.h"

#include "../camera/CameraTuning.h"
#include "../camera/PhotoCamera.h"
#include "../rmlui/RmlUiWorkspaceView.h"

namespace rs::app {

namespace {
using workspace::CameraScreenParameter;

// ファインダーへ入る。入力欄の焦点距離を、通常画面の視野角から換算した値へ合わせる(段には丸めない)。
void EnterFinder(FrameLoopState& loopState, rmlui::RmlUiCameraScreenView& screen, float fieldOfView) {
    screen.SetFocalLength(FocalLengthFromFovY(fieldOfView, kNormalViewFrameHeightMm));
    // 書けなかったときも、いまの入力欄の値を基準にする(そのまま出れば視野角は変わらない)。
    loopState.photoFinderEntryFocalMm = MakePhotoCamera(screen.Presentation()).focalLengthMm;
}

} // namespace

void ResolveFrameFieldOfView(FrameLoopState& loopState, FrameScratch& scratch) {
    CameraController& camera = loopState.globals.camera;
    // UIを隠している間は、ファインダーも出さない。
    rmlui::RmlUiWorkspaceView* const workspaceView = loopState.state.rmlUiIntegration.UiHidden()
                                                         ? nullptr
                                                         : loopState.state.rmlUiIntegration.WorkspaceView();
    const std::optional<workspace::CameraFinderRect> finder =
        workspaceView != nullptr ? workspaceView->CameraFinder() : std::nullopt;

    if (finder && !loopState.photoFinderActive) {
        EnterFinder(loopState, workspaceView->CameraScreen(), camera.fly.fieldOfView);
    } else if (!finder && loopState.photoFinderActive && workspaceView != nullptr) {
        const CameraTuning& tuning = CameraTuning::Embedded();
        const PhotoCamera photoCamera = MakePhotoCamera(workspaceView->CameraScreen().Presentation());
        if (const std::optional<float> restored = FieldOfViewOnFinderExit(
                loopState.photoFinderEntryFocalMm, photoCamera.focalLengthMm, tuning.fovYMinRadians,
                tuning.fovYMaxRadians)) {
            camera.fly.fieldOfView = *restored;
        }
    }
    loopState.photoFinderActive = finder.has_value();
    camera.fovAdjustLocked = finder.has_value();

    scratch.fieldOfView = camera.fly.fieldOfView;
    if (finder) {
        const PhotoCamera photoCamera = MakePhotoCamera(workspaceView->CameraScreen().Presentation());
        scratch.fieldOfView = FovYFromFocalLength(photoCamera.focalLengthMm, finder->width_ / finder->height_);
    }
}

} // namespace rs::app

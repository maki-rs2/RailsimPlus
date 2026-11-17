//
// AppRuntimeFramePhoto.cpp - フォトモード(ファインダー)の描画入力の決定と、ISO オートの測光結果の取り込み
//
#include "AppRuntimeFrameInternal.h"

#include <spdlog/spdlog.h>

#include "../camera/PhotoCamera.h"
#include "../rmlui/RmlUiWorkspaceView.h"
#include "../../platform/EnvironmentVariable.h"
#include "../../render/postprocess/LuminanceMeterPass.h"
#include "../../render/postprocess/TonemapMath.h"
#include "../../render/resource/RenderTuning.h"

namespace rs::app {

namespace {

// RS_AUTOISO のログの間隔[秒]。最初の測光は、すぐ出す。
constexpr double kAutoIsoLogIntervalSeconds = 3.0;

// 開発用: 環境変数 RS_AUTOISO があれば、ファインダーが出ていなくても ISO オートの測光を走らせ、測光した輝度と ISO を
// 数秒に1回ログへ出す(ヘッドレスで、ISO オートの目標の明るさを較正するため)。実際の露出は変えない。
bool AutoIsoForcedByEnvironment() {
    static const bool kForced = rs::platform::ReadEnvironmentVariable("RS_AUTOISO") != nullptr;
    return kForced;
}

AutoIsoParams MakeAutoIsoParams() {
    const rs::render::RenderTuning& tuning = rs::render::RenderTuning::Embedded();
    AutoIsoParams params;
    params.targetBrightness = tuning.photoAutoIsoTarget;
    params.tonemapExposure = rs::render::TonemapExposureFromTuning(tuning);
    params.followSeconds = tuning.photoAutoIsoSeconds;
    return params;
}

// ISO オートの1フレームぶん。追従を進めて、決まった ISO を返す。
double StepAutoIso(FrameLoopState& loopState, const PhotoCamera& camera, bool logIt) {
    const AutoIsoParams params = MakeAutoIsoParams();
    const bool newMeasurement = loopState.photoMeteredLuminance.has_value();
    const double dt = loopState.io.DeltaTime;
    const double iso = loopState.photoAutoIso.Update(dt, loopState.photoMeteredLuminance, camera.aperture,
                                                     camera.shutterSeconds, params);
    const std::optional<double> measured = loopState.photoMeteredLuminance;
    loopState.photoMeteredLuminance.reset();
    loopState.photoAutoIsoLogSeconds += dt;
    if (logIt && newMeasurement &&
        (!loopState.photoAutoIsoLogged || loopState.photoAutoIsoLogSeconds >= kAutoIsoLogIntervalSeconds)) {
        const std::optional<double> target =
            loopState.photoAutoIso.TargetIso(camera.aperture, camera.shutterSeconds, params);
        spdlog::info("[auto_iso] 測光した輝度={:.5f} 目標ISO={:.1f} 追従中のISO={:.1f} (絞り={:.1f} シャッター={:.5f}秒 目標の明るさ={:.3f} 露出={:.3f})",
                     *measured, target.value_or(0.0), iso, camera.aperture, camera.shutterSeconds,
                     params.targetBrightness, params.tonemapExposure);
        loopState.photoAutoIsoLogged = true;
        loopState.photoAutoIsoLogSeconds = 0.0;
    }
    return iso;
}

} // namespace

PhotoRenderInputs ResolvePhotoRender(FrameLoopState& loopState, FrameScratch& scratch) {
    PhotoRenderInputs inputs;
    scratch.view.aspect = 0.0f;
    // UIを隠している間は、ファインダーも出さない。
    rmlui::RmlUiWorkspaceView* const workspaceView = loopState.state.rmlUiIntegration.UiHidden()
                                                         ? nullptr
                                                         : loopState.state.rmlUiIntegration.WorkspaceView();
    const std::optional<workspace::CameraFinderRect> finder =
        workspaceView != nullptr ? workspaceView->CameraFinder() : std::nullopt;
    const bool forcedMeter = AutoIsoForcedByEnvironment();
    PhotoCamera photoCamera;
    if (workspaceView != nullptr && (finder || forcedMeter)) {
        photoCamera = MakePhotoCamera(workspaceView->CameraScreen().Presentation());
    }

    // ISO オートの測光は、ファインダーが出ていて ISO 欄が AUTO のときだけ走る(開発用の環境変数があれば、いつでも)。
    const bool autoIsoActive = finder.has_value() && photoCamera.isoAuto;
    inputs.luminanceMeter = autoIsoActive || forcedMeter;
    std::optional<double> autoIso;
    if (inputs.luminanceMeter) {
        autoIso = StepAutoIso(loopState, photoCamera, forcedMeter);
    } else {
        // 出た・AUTO を外れたときは捨てて、次に入ったとき ISO100 から始める。
        loopState.photoAutoIso.Reset();
        loopState.photoMeteredLuminance.reset();
        loopState.photoAutoIsoLogged = false;
        loopState.photoAutoIsoLogSeconds = 0.0;
    }

    if (finder) {
        inputs.finderRect = rs::render::OutputRect{static_cast<float>(finder->left_), static_cast<float>(finder->top_),
                                                   static_cast<float>(finder->width_), static_cast<float>(finder->height_)};
        scratch.view.aspect = static_cast<float>(finder->width_ / finder->height_);
        // 撮影カメラの焦点距離から決めた視野角(フレームの先頭で求めた値)。影・霧・AO・空は、この視野角を読む。
        scratch.view.fovYRadians = scratch.fieldOfView;
        inputs.dofCamera = rs::render::DepthOfFieldCamera{photoCamera.focalLengthMm, photoCamera.aperture,
                                                          photoCamera.focusDistanceM};
        const double iso = PhotoEffectiveIso(photoCamera, autoIso);
        inputs.exposureMultiplier =
            static_cast<float>(PhotoExposureMultiplier(iso, photoCamera.aperture, photoCamera.shutterSeconds));
        inputs.shutterSeconds = static_cast<float>(photoCamera.shutterSeconds);
        // ISOオート中は、決まったISOを撮影カメラの画面の7セグへ渡す。
        workspaceView->CameraScreen().SetAutoIso(photoCamera.isoAuto ? std::optional<double>(iso) : std::nullopt);
    }
    return inputs;
}

void CollectPhotoMeter(FrameLoopState& loopState) {
    const std::optional<rs::render::LuminanceMeterGrid> grid = loopState.globals.app.PollLuminanceMeter();
    if (!grid) return;
    loopState.photoMeteredLuminance = MeteredLuminance(grid->logLuminance, rs::render::LuminanceMeterGrid::kWidth,
                                                       rs::render::LuminanceMeterGrid::kHeight);
}

} // namespace rs::app

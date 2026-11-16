#include "App.h"

#include <chrono>

#include <spdlog/spdlog.h>

#include "../../render/resource/GeometryManager.h"
#include "../../render/postprocess/PostProcessFormats.h"
#include "../../render/postprocess/TonemapMath.h"
#include "../../render/resource/RenderTuning.h"
#include "../../render/resource/ShaderCache.h"
#include "../rmlui/RmlUiIntegration.h"
#include "InitSteps.h"

namespace rs::app {

// トーンマップのPSOは出力形式を固定で持つので、バックバッファのシーン用RTVと一致させる。
static_assert(kBackBufferRtvFormat == rs::render::kTonemapOutputFormat);

namespace {

// ビューアーの背景色(sRGB)から、トーンマップ後にその色になるHDRシーンターゲットのクリア値を求める。
void ViewerClearLinear(const rs::render::TonemapParams& tonemapParams, const float backgroundSrgb[3],
                       float clearLinear[3]) {
    for (int i = 0; i < 3; ++i) {
        clearLinear[i] =
            rs::render::InverseTonemapChannel(tonemapParams, rs::render::SrgbToLinear(backgroundSrgb[i]));
    }
}

} // namespace

void App::SetViewerBackground(bool lightOn) {
    if (!viewerBackground_ || lightOn == viewerBackgroundLightOn_) return;
    viewerBackgroundLightOn_ = lightOn;
    dirty_.Mark(RenderRebuild::ViewerBackground);
}

void App::SetAmbientOcclusion(rs::render::AoQuality quality) {
    if (quality == renderer_.AmbientOcclusionQuality()) return;
    renderer_.SetAmbientOcclusion(quality);
    dirty_.Mark(RenderRebuild::AoQuality);
}

void App::SetPostProcessSettings(const rs::render::PostProcessSettings& settings) {
    postProcess_.SetSettings(settings);
    const bool taa = rs::render::TemporalAaActive(settings);
    const bool motionBlur = rs::render::MotionBlurActive(settings);
    if (taa != renderer_.TemporalAaEnabled()) {
        renderer_.SetTemporalAa(taa);
        dirty_.Mark(RenderRebuild::TemporalAa);
    }
    if (motionBlur != renderer_.MotionBlurEnabled()) {
        renderer_.SetMotionBlur(motionBlur);
        dirty_.Mark(RenderRebuild::MotionBlur);
    }
    if (settings.volumetricFog != renderer_.VolumetricFogEnabled()) {
        renderer_.SetVolumetricFog(settings.volumetricFog);
        dirty_.Mark(RenderRebuild::VolumetricFog);
    }
}

void App::SetDepthOfFieldCamera(const std::optional<rs::render::DepthOfFieldCamera>& camera) {
    const bool wasEnabled = renderer_.DepthOfFieldEnabled();
    renderer_.SetDepthOfFieldCamera(camera);
    if (renderer_.DepthOfFieldEnabled() != wasEnabled) dirty_.Mark(RenderRebuild::DepthOfField);
}

void App::SetLuminanceMeter(bool enabled) {
    if (enabled == renderer_.LuminanceMeterEnabled()) return;
    renderer_.SetLuminanceMeter(enabled);
    dirty_.Mark(RenderRebuild::LuminanceMeter);
}

void App::SetShadowQuality(rs::render::ShadowQuality quality) {
    pendingShadowQuality_ = quality;
    if (renderer_.ShadowMapNeedsRebuild(quality)) dirty_.Mark(RenderRebuild::ShadowMap);
    renderer_.SetShadowQuality(quality);
}

void App::OnResize(UINT width, UINT height) {
    pendingWidth_ = width;
    pendingHeight_ = height;
    dirty_.Mark(RenderRebuild::Resize);
    // スワップチェーンの大きさは、ウィンドウの大きさに合わせてすぐ変える(サイズ変更中のモーダルループでも描くため)。
    FlushRebuilds();
}

void App::FlushRebuilds() {
    const RenderRebuild dirty = dirty_.Take();
    if (dirty == RenderRebuild::None) return;
    // 作り直す資源をGPUが使い終えるのを、まとめて1回だけ待つ。
    gpu_.WaitForGpu();
    const RebuildPlan plan = PlanRebuild(dirty);
    for (std::size_t i = 0; i < plan.count; ++i) {
        switch (plan.actions[i]) {
        case RebuildAction::Swapchain:
            if (gpu_.OnResize(pendingWidth_, pendingHeight_)) {
                renderer_.OnResize(gpu_.Device(), pendingWidth_, pendingHeight_);
                if (!hdrTarget_.Resize(pendingWidth_, pendingHeight_))
                    spdlog::error("[app] HDRシーンターゲットの再作成に失敗しました");
                postProcess_.Resize(pendingWidth_, pendingHeight_);
                if (rmlUiIntegration_ != nullptr) rmlUiIntegration_->OnResize(pendingWidth_, pendingHeight_);
            }
            break;
        case RebuildAction::HdrClearColor: {
            float clearLinear[3] = {};
            ViewerClearLinear(tonemap_.Params(),
                              viewerBackgroundLightOn_ ? kViewerBackgroundDaySrgb : kViewerBackgroundNightSrgb,
                              clearLinear);
            if (!hdrTarget_.SetClearColor(clearLinear)) spdlog::error("[app] ビューアーの背景色を変えられませんでした");
            break;
        }
        case RebuildAction::SizedPasses:
            renderer_.EnsureSizedResources();
            break;
        case RebuildAction::ShadowMap:
            // 品質を続けて変えたときは、最後の品質に対してだけ作り直す。
            if (renderer_.ShadowMapNeedsRebuild(pendingShadowQuality_)) {
                renderer_.RebuildShadowMapFor(rs::rhi::Device(gpu_.Device()),
                                              rs::rhi::SrvHeap(gpu_.SrvHeap(), gpu_.SrvStride()), pendingShadowQuality_);
            }
            break;
        }
    }
}

bool App::Init(HWND hwnd, UINT width, UINT height, bool viewerBackground) {
    // 起動フェーズ別の所要時間(device+psoの内訳)。
    using InitClock = std::chrono::steady_clock;
    const InitClock::time_point t0 = InitClock::now();
    // 通常は背景色(sRGB)をリニアへ変換するだけ。ビューアーモードは、トーンマップ後に目標の色になる
    // HDR値を逆算して渡す(トーンマップは消去値にも同じ式で掛かる。tonemap_.Init と同じパラメータを使う)。
    // ビューアーは昼(ライトON)の色で始め、ライトの切替は FlushRebuilds が反映する。
    const rs::render::TonemapParams tonemapParams = rs::render::LoadTonemapParams();
    viewerBackground_ = viewerBackground;
    float clearLinear[3] = {};
    if (viewerBackground) {
        ViewerClearLinear(tonemapParams, kViewerBackgroundDaySrgb, clearLinear);
    } else {
        for (int i = 0; i < 3; ++i) clearLinear[i] = rs::render::SrgbToLinear(kBackgroundClearSrgb[i]);
    }
    pendingWidth_ = width;
    pendingHeight_ = height;

    InitClock::time_point t1 = t0;
    // デバイス・ヒープ・キューは gpu_ の初期化後に作る(各手順の中で取り出す)。
    auto rhiDevice = [this] { return rs::rhi::Device(gpu_.Device()); };
    auto rhiSrvHeap = [this] { return rs::rhi::SrvHeap(gpu_.SrvHeap(), gpu_.SrvStride()); };
    auto rhiQueue = [this] { return rs::rhi::CommandQueue(gpu_.Queue()); };
    using Importance = InitImportance;
    // 必須: デバイス・スワップチェーン・主パス(FrameRenderer)・HDRシーンとトーンマップ・Envのキューブ。
    // 任意: プラグインの空の天体・レンズフレア・標準の空の遠景・ブルームなどのポストプロセス。
    // 任意の失敗は、そのパスだけを使えなくして起動を続ける。
    const InitStep steps[] = {
        {"gpu", Importance::Required,
         [&] {
             if (!gpu_.Init(hwnd, width, height, kSrvLayout.total)) return false;
             t1 = InitClock::now();
             return true;
         }},
        {"frameRenderer", Importance::Required,
         [&] {
             return renderer_.Init(gpu_.Device(), gpu_.SrvHeap(), gpu_.SrvStride(), width, height, gpu_.Retirements());
         }},
        // Envの焼き済みキューブ(アップロードはキュー越しに完了を待つため、デバイス・ヒープ・キューを渡す)。
        {"envCubes", Importance::Required,
         [&] { return renderer_.EnvCubes().Init(rhiDevice(), rhiSrvHeap(), rhiQueue(), kSrvLayout.envCube); }},
        // プラグインの空の Sun.x・Moon.x(失敗してもその天体を描かないだけ)。
        {"skyBodies", Importance::Optional,
         [&] { return renderer_.SkyBodies().Init(rhiDevice(), rhiSrvHeap(), rhiQueue(), kSrvLayout.skyBody); }},
        // 太陽のレンズフレアと白飛び(作れなければ出さないだけ)。
        {"lensFlare", Importance::Optional,
         [&] { return renderer_.InitLensFlare(rhiDevice(), rhiSrvHeap(), rhiQueue(), kSrvLayout.lensFlare); }},
        // 標準の空の遠景(画像が読めなければ遠景なしで動く)。
        {"landscape", Importance::Optional,
         [&] { return renderer_.Landscape().Init(rhiDevice(), rhiSrvHeap(), rhiQueue(), kSrvLayout.landscape); }},
        // HDRシーンターゲットとトーンマップ。HDRのクリア値は背景色(sRGB)をリニアへ変換したもの
        // (パススルーにするとバックバッファのクリア色と同じバイト値が出る)。
        {"hdrTarget", Importance::Required,
         [&] {
             if (!hdrTarget_.Init(rhiDevice(), rhiSrvHeap(), kSrvLayout.sceneHdr, clearLinear) ||
                 !hdrTarget_.Resize(width, height))
                 return false;
             renderer_.SetSceneHdrTarget(&hdrTarget_);
             return true;
         }},
        {"tonemap", Importance::Required, [&] { return tonemap_.Init(rhiDevice(), tonemapParams); }},
        // ブルームなどのポストプロセス(失敗してもブルームが掛からないだけ)。
        {"postProcess", Importance::Optional,
         [&] {
             const rs::render::RenderTuning& tuning = rs::render::RenderTuning::Embedded();
             postProcess_.SetTuning({tuning.bloomStrength, tuning.bloomThreshold, tuning.bloomScatter});
             return postProcess_.Init(rhiDevice(), rhiSrvHeap(), kSrvLayout.postProcess) &&
                    postProcess_.Resize(width, height);
         }},
    };
    if (!RunInitSteps(steps).ok) return false;
    const InitClock::time_point t2 = InitClock::now();
    spdlog::debug("[startup] device内訳: d3d12={:.0f}ms renderer(PSO/シェーダ)={:.0f}ms",
                 std::chrono::duration<double, std::milli>(t1 - t0).count(),
                 std::chrono::duration<double, std::milli>(t2 - t1).count());
    const rs::render::ShaderCacheStats shaderCacheStats = rs::render::GetShaderCacheStats();
    spdlog::debug("[startup] シェーダキャッシュ: 読込={} コンパイル={}", shaderCacheStats.hits,
                  shaderCacheStats.compiles);
    return true;
}

void App::Render(const CameraView& cam, bool skipMeshPass) {
    if (gpu_.DeviceLost()) return;
    CpuRenderBreakdown breakdown;
    {
        const ScopedAccumulate appRenderScope(breakdown.appRenderMs);
        RenderTimed(cam, skipMeshPass, breakdown);
    }
    // FrameRenderer の分(Render の中で測った区間)を足す。
    const CpuRenderBreakdown& rendererPart = renderer_.LastCpuBreakdown();
    breakdown.rendererMs = rendererPart.rendererMs;
    breakdown.cullMs = rendererPart.cullMs;
    breakdown.batchSyncMs = rendererPart.batchSyncMs;
    breakdown.shadowCpuMs = rendererPart.shadowCpuMs;
    breakdown.meshRecordMs = rendererPart.meshRecordMs;
    cpuBreakdown_ = breakdown;
}

void App::Shutdown() {
    gpu_.Shutdown();
    // シングルトンが残したGPUバッファを、デバイスより長く生かさない。
    rs::render::GetGeometryManager().ClearRecent();
}

} // namespace rs::app

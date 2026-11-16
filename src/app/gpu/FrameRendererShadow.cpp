#include "../../render/RenderPrecision.h"
#include "FrameRenderer.h"
#include "App.h"

#include "../../core/Epsilon.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>

#include <spdlog/spdlog.h>

#include "../../render/resource/RenderTuning.h"
#include "../../render/shadow/CascadeFit.h"
#include "../../render/shadow/ShadowUpdateSchedule.h"
#include "../../render/shadow/ShadowConstants.h"
#include "../../core/Frustum.h"
#include "CameraMath.h"
#include "SceneSlotCulling.h"
#include "GpuContext.h"

// FrameRenderer の太陽光の影(シャドウマップの用意・影定数の組み立て・影パスの記録)。

namespace rs::app {

namespace {

// 太陽の高度[度]。sunDirection は光が進む向きなので、太陽そのものは符号反転。
double SunElevationDeg(const rs::core::Vec3& sunDirection) {
    const double sinElevation = std::clamp(-static_cast<double>(sunDirection.y), -1.0, 1.0);
    return std::asin(sinElevation) * 180.0 / std::numbers::pi;
}

// 時刻による影の強さの倍率(0..1)。昼夜係数に合わせて下げ、太陽の高度が下限以下ならゼロ、下限から
// 幅の分だけ上がると1になる。
float SunShadowTimeFactor(double elevationDeg, float dayAlpha, const rs::render::RenderTuning& tuning) {
    const double fadeWidth = std::max<double>(tuning.shadowSunElevationFadeDeg, rs::core::kShadowFadeAngleEpsDeg);
    const double elevationFactor =
        std::clamp((elevationDeg - tuning.shadowMinSunElevationDeg) / fadeWidth, 0.0, 1.0);
    return rs::render::ToGpuFloat(elevationFactor) * dayAlpha;
}

} // namespace

bool FrameRenderer::InitShadow(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap) {
    // 品質を切り替えるときは RebuildShadowMapFor で大きさを合わせる。起動時は既定の品質で作る。
    const rs::render::ShadowPresetSpec spec = rs::render::ShadowSpecOf(rs::render::kDefaultShadowQuality);
    if (!shadowMap_.Init(device, srvHeap, kSrvLayout.shadowMap, spec.mapSize, spec.cascadeCount) ||
        !shadowPass_.Init(device)) {
        spdlog::warn("[shadow] シャドウマップの作成に失敗しました。影は描きません");
        return false;
    }
    return true;
}

bool FrameRenderer::ShadowMapNeedsRebuild(rs::render::ShadowQuality quality) const {
    const rs::render::ShadowPresetSpec spec = rs::render::ShadowSpecOf(quality);
    if (spec.cascadeCount == 0) return false;
    return !shadowMap_.Ready() || shadowMap_.Size() != spec.mapSize ||
           shadowMap_.SliceCount() != spec.cascadeCount;
}

bool FrameRenderer::RebuildShadowMapFor(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap,
                                        rs::render::ShadowQuality quality) {
    shadowScheduleHistoryValid_ = false;
    const rs::render::ShadowPresetSpec spec = rs::render::ShadowSpecOf(quality);
    if (spec.cascadeCount == 0) return true;
    if (!shadowMap_.Init(device, srvHeap, kSrvLayout.shadowMap, spec.mapSize, spec.cascadeCount)) {
        spdlog::error("[shadow] シャドウマップの作り直しに失敗しました");
        return false;
    }
    return true;
}

bool FrameRenderer::PrepareShadow(const CameraView& cam, const ViewProjResult& viewProj, UINT width,
                                  UINT height, rs::render::MeshConstants& constants, UINT frameIndex) {
    if (!ShadowsEnabled() || !shadowMap_.Ready() || !shadowPass_.Ready()) {
        shadowScheduleHistoryValid_ = false;
        return false;
    }
    const rs::render::RenderTuning& tuning = rs::render::RenderTuning::Embedded();

    // 夜・太陽が低いとき(影が極端に長くなる)は、パスごと飛ばす。
    const float timeFactor = SunShadowTimeFactor(SunElevationDeg(sunDirection_), dayAlpha_, tuning);
    if (timeFactor <= 0.0f) {
        shadowScheduleHistoryValid_ = false;
        return false;
    }

    const rs::render::ShadowPresetSpec spec = rs::render::ShadowSpecOf(shadowQuality_);
    rs::render::CascadeFitInput fitInput;
    fitInput.view = viewProj.view;
    fitInput.fovYRadians = cam.fovYRadians;
    fitInput.aspect = EffectiveAspect(cam, width, height);
    fitInput.nearDistance = kCameraNearM;
    fitInput.farDistance = cam.renderDistance;
    fitInput.shadowDistance = rs::render::ShadowDistanceMeters(tuning, shadowDistance_);
    fitInput.lightDirection = sunDirection_;
    fitInput.shadowMapSize = spec.mapSize;
    fitInput.casterExtensionM = tuning.shadowCasterExtensionM;
    fitInput.cascadeCount = spec.cascadeCount;
    fitInput.splitLambda = tuning.shadowSplitLambda;
    const float blendFraction = spec.cascadeBlend ? std::clamp(tuning.shadowCascadeBlendFraction, 0.0f, 0.5f) : 0.0f;
    fitInput.blendFraction = blendFraction;
    const rs::render::CascadeFitOutput fit = rs::render::FitCascades(fitInput);
    if (fit.count == 0) {
        shadowScheduleHistoryValid_ = false;
        return false;
    }

    if (shadowScheduleCascadeCount_ != fit.count) shadowScheduleHistoryValid_ = false;
    bool farSnapChanged = false;
    if (shadowScheduleHistoryValid_) {
        for (std::uint32_t c = 2; c < fit.count; ++c) {
            if (fit.cascades[c].snapX != retainedShadowCascades_[c].snapX ||
                fit.cascades[c].snapY != retainedShadowCascades_[c].snapY) {
                farSnapChanged = true;
                break;
            }
        }
    }
    const std::uint32_t updateMask = rs::render::ShadowUpdateMask(
        shadowFrameCounter_, fit.count, sunDirection_, lastFullShadowSunDirection_,
        tuning.shadowFarCascadeSunAngleDeg, shadowScheduleHistoryValid_, farSnapChanged);
    const std::uint32_t allMask = (1u << fit.count) - 1u;

    // 影を落とすもの: 表示中のスロットのうち、各カスケードのライト用視錐台に入るもの。視点のカリング
    // 結果は使わない(画面の外にいても、影が画面の中へ伸びてくる)。バッチは、1個もライト用視錐台に入らないものだけ飛ばす。
    bool anyCaster = instanceBatches_.BatchCount() > 0 || trainInstanceBatches_.BatchCount() > 0;
    shadowCascadeCount_ = fit.count;
    for (std::uint32_t c = 0; c < fit.count; ++c) {
        ShadowCascadeDraw& draw = shadowCascades_[c];
        draw.update = (updateMask & (1u << c)) != 0;
        if (draw.update) retainedShadowCascades_[c] = fit.cascades[c];
        draw.viewProj = retainedShadowCascades_[c].viewProj;
        draw.lightFrustum = rs::core::ExtractFrustum(draw.viewProj);
        draw.casters.clear();
        if (!draw.update) continue;
        const bool includeThin = spec.thinCasters && c < static_cast<std::uint32_t>(std::max(tuning.shadowThinCascadeCount, 0));
        if (meshPipeline_.HasMesh())
            draw.casters.push_back({&meshPipeline_, static_cast<UINT>(meshPipeline_.PartXforms().size()), includeThin});
        for (size_t i = 0; i < scenePipelines_.size(); ++i) {
            if (!scenePipelines_[i].HasMesh() || !IsSceneSlotVisible(i) ||
                !(sceneSlotState_.FadeAlpha()[i] > 0.0f))
                continue;
            const bool inRange = i >= sceneSlotState_.Bounds().size() ||
                                 IsSlotInShadowFrustum(sceneSlotState_.Bounds()[i], draw.lightFrustum);
            if (inRange)
                draw.casters.push_back({&scenePipelines_[i], static_cast<UINT>(scenePipelines_[i].PartXforms().size()), includeThin});
        }
        anyCaster = anyCaster || !draw.casters.empty();
    }
    if (!anyCaster) {
        shadowScheduleHistoryValid_ = false;
        return false;
    }

    rs::render::ShadowConstants shadowConstants = {};
    for (std::uint32_t i = 0; i < fit.count; ++i) {
        std::memcpy(shadowConstants.viewProj[i], retainedShadowCascades_[i].viewProj.m,
                    sizeof(shadowConstants.viewProj[i]));
        shadowConstants.splitFar[i] = fit.cascades[i].farSplit;
        shadowConstants.texelWorldSize[i] = retainedShadowCascades_[i].texelWorldSize;
    }
    // 実際に影を引く距離は、フィットが使った(遠クリップで打ち切り済みの)距離。
    const float shadowDistance = fit.cascades[fit.count - 1].farSplit;
    shadowConstants.params[0] = 1.0f;
    shadowConstants.params[1] = std::clamp(tuning.shadowStrength, 0.0f, 1.0f) * timeFactor;
    shadowConstants.params[2] = tuning.shadowNormalOffsetTexels;
    shadowConstants.params[3] = blendFraction;
    shadowConstants.params2[0] = shadowDistance;
    shadowConstants.params2[1] = shadowDistance * (1.0f - std::clamp(tuning.shadowFadeFraction, 0.0f, 1.0f));
    shadowConstants.params2[2] = std::max(spec.narrowBlur ? tuning.shadowBlurRadiusNarrowM : tuning.shadowBlurRadiusM, 0.0f);
    shadowConstants.params2[3] = static_cast<float>(spec.blurTapCount);
    shadowConstants.params3[0] = std::max(tuning.shadowPcssLightSize, 0.0f);
    shadowConstants.params3[1] = std::max(tuning.shadowPcssMaxRadiusM, shadowConstants.params2[2]);
    shadowConstants.params3[2] = std::max(tuning.shadowPcssBlockerBiasM, 0.0f);
    shadowConstants.params3[3] = spec.filter == rs::render::ShadowFilter::Pcss ? 1.0f : 0.0f;
    shadowConstants.index[0] = shadowMap_.SrvIndex();
    shadowConstants.index[1] = fit.count;

    const std::uint64_t address = shadowPass_.WriteConstants(frameIndex, shadowConstants);
    if (address == 0) {
        shadowScheduleHistoryValid_ = false;
        return false;
    }
    constants.shadowConstantsAddress = address;
    shadowScheduleHistoryValid_ = true;
    shadowScheduleCascadeCount_ = fit.count;
    if (updateMask == allMask) lastFullShadowSunDirection_ = sunDirection_;
    return true;
}

void FrameRenderer::RecordShadow(const rs::rhi::CommandList& commandList, GpuFrameTimer* timer) {
    const ScopedAccumulate rendererScope(cpuBreakdown_.rendererMs);
    const ScopedAccumulate shadowScope(cpuBreakdown_.shadowCpuMs);
    const rs::render::MeshConstants& constants = frame_.constants;
    const rs::render::ViewConstants& mainView = frame_.mainView;
    const UINT frameIndex = frame_.frameIndex;
    ID3D12GraphicsCommandList* cmdList = commandList.Native();
    if (timer) timer->Begin(cmdList, kGpuRegionShadow);
    for (std::uint32_t c = 0; c < shadowCascadeCount_; ++c) {
        const ShadowCascadeDraw& draw = shadowCascades_[c];
        if (!draw.update) continue;
        const rs::render::ShadowExtraRecord recordBatches =
            [this, &commandList, &draw, frameIndex, c](const rs::render::MeshConstants& lightConstants) {
                if (instanceBatches_.BatchCount() > 0)
                    instanceBatches_.RecordShadow(commandList, lightConstants, frameIndex, c, &draw.lightFrustum);
                if (trainInstanceBatches_.BatchCount() > 0)
                    trainInstanceBatches_.RecordShadow(commandList, lightConstants, frameIndex, c, &draw.lightFrustum);
            };
        // このカスケードの視点: カメラ位置などはメインのまま、viewProj だけをライトのものにする。
        rs::render::ViewConstants cascadeView = mainView;
        std::memcpy(cascadeView.viewProj, draw.viewProj.m, sizeof(cascadeView.viewProj));
        std::memcpy(cascadeView.prevViewProj, draw.viewProj.m, sizeof(cascadeView.prevViewProj)); // 影は「前 = 今」
        rs::render::MeshConstants lightBase = constants;
        lightBase.viewConstantsAddress = viewConstants_.Write(frameIndex, kViewSlotShadowFirst + c, cascadeView);
        shadowPass_.Record(commandList, shadowMap_, c, lightBase, draw.casters, recordBatches, frameIndex);
    }

    // 影パスが書き換えた出力先とビューポートを、本描画の分へ戻す。
    RestoreMainTargets(commandList);
    if (timer) timer->End(cmdList, kGpuRegionShadow);
    ++shadowFrameCounter_;
}

} // namespace rs::app

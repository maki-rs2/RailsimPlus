#include "RenderTuning.h"

#include "../../core/ConfigJson.h"

#include <cmath>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "EmbeddedRenderTuning.h"
#include "../../core/TuningSlot.h"

namespace rs::render {

namespace {

// キーが無い・数値でない・NaN・範囲外のときは fallback を返す。
float ReadInRange(const nlohmann::json& root, const char* key, float fallback, float minValue, float maxValue) {
    const float value = root.value(key, fallback);
    if (!std::isfinite(value) || value < minValue || value > maxValue) return fallback;
    return value;
}

// オブジェクトのJSONから各キーを読む。キーの型が違うと nlohmann::json::exception を投げる。
RenderTuning ReadTuning(const nlohmann::json& root) {
    RenderTuning tuning;
    tuning.modelCacheBudgetBytes =
        root.value("modelCacheBudgetBytes", tuning.modelCacheBudgetBytes);
    tuning.textureCacheBudgetBytes =
        root.value("textureCacheBudgetBytes", tuning.textureCacheBudgetBytes);
    tuning.cutoutOccluderAlphaThreshold = root.value(
        "cutoutOccluderAlphaThreshold", tuning.cutoutOccluderAlphaThreshold);
    tuning.semiAlphaLow = root.value("semiAlphaLow", tuning.semiAlphaLow);
    tuning.semiAlphaHigh = root.value("semiAlphaHigh", tuning.semiAlphaHigh);
    tuning.semiAlphaMinFraction = root.value("semiAlphaMinFraction", tuning.semiAlphaMinFraction);
    tuning.ambientIntensity = root.value("ambientIntensity", tuning.ambientIntensity);
    tuning.nightAmbientIntensity =
        root.value("nightAmbientIntensity", tuning.nightAmbientIntensity);
    tuning.nightAmbientFloor = root.value("nightAmbientFloor", tuning.nightAmbientFloor);
    tuning.specularAaStrength = root.value("specularAaStrength", tuning.specularAaStrength);
    tuning.nightAdaptationStart = root.value("nightAdaptationStart", tuning.nightAdaptationStart);
    tuning.nightAdaptationExponent = root.value("nightAdaptationExponent", tuning.nightAdaptationExponent);
    tuning.nightAdaptationMaxGain = root.value("nightAdaptationMaxGain", tuning.nightAdaptationMaxGain);
    tuning.nightAdaptationBoost = root.value("nightAdaptationBoost", tuning.nightAdaptationBoost);
    tuning.dayAdaptationStart = root.value("dayAdaptationStart", tuning.dayAdaptationStart);
    tuning.dayAdaptationExponent = root.value("dayAdaptationExponent", tuning.dayAdaptationExponent);
    tuning.tonemapExposure = root.value("tonemapExposure", tuning.tonemapExposure);
    tuning.skyRadianceScale = root.value("skyRadianceScale", tuning.skyRadianceScale);
    tuning.sunDiskIntensity = root.value("sunDiskIntensity", tuning.sunDiskIntensity);
    tuning.sunDiskAngularDiameterDeg =
        root.value("sunDiskAngularDiameterDeg", tuning.sunDiskAngularDiameterDeg);
    tuning.atmosphereAerosolOpticalDepth =
        root.value("atmosphereAerosolOpticalDepth", tuning.atmosphereAerosolOpticalDepth);
    tuning.atmosphereFollowCameraHeight =
        root.value("atmosphereFollowCameraHeight", tuning.atmosphereFollowCameraHeight);
    // APのLUTの大きさ。2未満(スライスの割り算が0除算になる)・巨大(テクスチャの上限)・NaNは既定値へ戻す。
    const RenderTuning defaults;
    tuning.aerialPerspectiveSliceCount =
        root.value("aerialPerspectiveSliceCount", tuning.aerialPerspectiveSliceCount);
    tuning.aerialPerspectiveTileSize = root.value("aerialPerspectiveTileSize", tuning.aerialPerspectiveTileSize);
    tuning.aerialPerspectiveMaxDistanceKm =
        root.value("aerialPerspectiveMaxDistanceKm", tuning.aerialPerspectiveMaxDistanceKm);
    if (tuning.aerialPerspectiveSliceCount < 2 || tuning.aerialPerspectiveSliceCount > kAerialMaxSliceCount)
        tuning.aerialPerspectiveSliceCount = defaults.aerialPerspectiveSliceCount;
    if (tuning.aerialPerspectiveTileSize < 2 || tuning.aerialPerspectiveTileSize > kAerialMaxTileSize)
        tuning.aerialPerspectiveTileSize = defaults.aerialPerspectiveTileSize;
    if (!std::isfinite(tuning.aerialPerspectiveMaxDistanceKm) || tuning.aerialPerspectiveMaxDistanceKm <= 0.0f)
        tuning.aerialPerspectiveMaxDistanceKm = defaults.aerialPerspectiveMaxDistanceKm;
    // 高さフォグ。範囲外・NaNは既定値へ戻す。
    tuning.heightFogDensity = ReadInRange(root, "heightFogDensity", defaults.heightFogDensity, 0.0f, 0.05f);
    tuning.heightFogHeightScale =
        ReadInRange(root, "heightFogHeightScale", defaults.heightFogHeightScale, 1.0f, 5000.0f);
    tuning.heightFogBaseHeight =
        ReadInRange(root, "heightFogBaseHeight", defaults.heightFogBaseHeight, -1000.0f, 10000.0f);
    tuning.heightFogAlbedo = ReadInRange(root, "heightFogAlbedo", defaults.heightFogAlbedo, 0.0f, 1.0f);
    tuning.iblRegenerateThresholdDeg =
        root.value("iblRegenerateThresholdDeg", tuning.iblRegenerateThresholdDeg);
    tuning.iblDiffuseIntensity = root.value("iblDiffuseIntensity", tuning.iblDiffuseIntensity);
    tuning.iblSpecularIntensity = root.value("iblSpecularIntensity", tuning.iblSpecularIntensity);
    tuning.iblSpecularMipScale = root.value("iblSpecularMipScale", tuning.iblSpecularMipScale);
    tuning.photoSpecularGain = root.value("photoSpecularGain", tuning.photoSpecularGain);
    tuning.pluginSkyDiffuseIntensity =
        root.value("pluginSkyDiffuseIntensity", tuning.pluginSkyDiffuseIntensity);
    tuning.pluginSkySpecularIntensity =
        root.value("pluginSkySpecularIntensity", tuning.pluginSkySpecularIntensity);
    // 0以下は背景が真っ黒になるので既定値へ戻す(ReadInRange の下限は含むため、0は別に弾く)。
    tuning.pluginSkyAtmosphereScale =
        ReadInRange(root, "pluginSkyAtmosphereScale", defaults.pluginSkyAtmosphereScale, 0.0f, 100.0f);
    if (tuning.pluginSkyAtmosphereScale <= 0.0f) tuning.pluginSkyAtmosphereScale = defaults.pluginSkyAtmosphereScale;
    tuning.pluginSkyBodies = root.value("pluginSkyBodies", tuning.pluginSkyBodies);
    // 負・NaN・infは既定値へ戻す。
    const float defaultFlareIntensity = tuning.flareHdrIntensity;
    const float defaultWhiteoutLevel = tuning.whiteoutHdrLevel;
    tuning.flareHdrIntensity = root.value("flareHdrIntensity", defaultFlareIntensity);
    tuning.whiteoutHdrLevel = root.value("whiteoutHdrLevel", defaultWhiteoutLevel);
    if (!std::isfinite(tuning.flareHdrIntensity) || tuning.flareHdrIntensity < 0.0f)
        tuning.flareHdrIntensity = defaultFlareIntensity;
    if (!std::isfinite(tuning.whiteoutHdrLevel) || tuning.whiteoutHdrLevel < 0.0f)
        tuning.whiteoutHdrLevel = defaultWhiteoutLevel;
    tuning.bloomThreshold = ReadInRange(root, "bloomThreshold", defaults.bloomThreshold, 0.0f, 100.0f);
    tuning.bloomStrength = ReadInRange(root, "bloomStrength", defaults.bloomStrength, 0.0f, 10.0f);
    tuning.bloomScatter = ReadInRange(root, "bloomScatter", defaults.bloomScatter, 0.0f, 1.0f);
    tuning.taaHistoryWeight = ReadInRange(root, "taaHistoryWeight", defaults.taaHistoryWeight, 0.0f, 0.99f);
    tuning.taaCutDistanceM = ReadInRange(root, "taaCutDistanceM", defaults.taaCutDistanceM, 0.1f, 100000.0f);
    tuning.taaSharpness = ReadInRange(root, "taaSharpness", defaults.taaSharpness, 0.0f, 2.0f);
    tuning.volumetricFogDistance =
        ReadInRange(root, "volumetricFogDistance", defaults.volumetricFogDistance, 10.0f, 2000.0f);
    tuning.volumetricFogGridPixelSize = static_cast<int>(ReadInRange(
        root, "volumetricFogGridPixelSize", static_cast<float>(defaults.volumetricFogGridPixelSize), 4.0f, 64.0f));
    tuning.volumetricFogSliceCount = static_cast<int>(ReadInRange(
        root, "volumetricFogSliceCount", static_cast<float>(defaults.volumetricFogSliceCount), 8.0f, 128.0f));
    tuning.volumetricFogAnisotropy =
        ReadInRange(root, "volumetricFogAnisotropy", defaults.volumetricFogAnisotropy, -0.9f, 0.9f);
    tuning.sunLightScale = ReadInRange(root, "sunLightScale", defaults.sunLightScale, 0.0f, 100.0f);
    tuning.volumetricFogSunIntensity =
        ReadInRange(root, "volumetricFogSunIntensity", defaults.volumetricFogSunIntensity, 0.0f, 100.0f);
    tuning.volumetricFogHistoryWeight =
        ReadInRange(root, "volumetricFogHistoryWeight", defaults.volumetricFogHistoryWeight, 0.0f, 0.98f);
    tuning.motionBlurShutter = ReadInRange(root, "motionBlurShutter", defaults.motionBlurShutter, 0.0f, 2.0f);
    tuning.motionBlurMaxRadius = ReadInRange(root, "motionBlurMaxRadius", defaults.motionBlurMaxRadius, 0.001f, 0.1f);
    tuning.photoMotionBlurMaxRadius =
        ReadInRange(root, "photoMotionBlurMaxRadius", defaults.photoMotionBlurMaxRadius, 0.001f, 0.1f);
    tuning.motionBlurSamples = static_cast<int>(
        ReadInRange(root, "motionBlurSamples", static_cast<float>(defaults.motionBlurSamples), 4.0f, 32.0f));
    tuning.dofMaxRadius = ReadInRange(root, "dofMaxRadius", defaults.dofMaxRadius, 0.001f, 0.1f);
    tuning.dofSamples =
        static_cast<int>(ReadInRange(root, "dofSamples", static_cast<float>(defaults.dofSamples), 8.0f, 64.0f));
    tuning.photoAutoIsoTarget = ReadInRange(root, "photoAutoIsoTarget", defaults.photoAutoIsoTarget, 0.001f, 10.0f);
    tuning.photoAutoIsoSeconds = ReadInRange(root, "photoAutoIsoSeconds", defaults.photoAutoIsoSeconds, 0.0f, 10.0f);
    tuning.aoRadius = ReadInRange(root, "aoRadius", defaults.aoRadius, 0.05f, 20.0f);
    tuning.aoFalloffRange = ReadInRange(root, "aoFalloffRange", defaults.aoFalloffRange, 0.0f, 1.0f);
    tuning.aoFinalPower = ReadInRange(root, "aoFinalPower", defaults.aoFinalPower, 0.1f, 8.0f);
    tuning.landscapeBrightness = root.value("landscapeBrightness", tuning.landscapeBrightness);
    tuning.landscapeSaturation = root.value("landscapeSaturation", tuning.landscapeSaturation);
    tuning.landscapeContrast = root.value("landscapeContrast", tuning.landscapeContrast);
    tuning.landscapeTintR = root.value("landscapeTintR", tuning.landscapeTintR);
    tuning.landscapeTintG = root.value("landscapeTintG", tuning.landscapeTintG);
    tuning.landscapeTintB = root.value("landscapeTintB", tuning.landscapeTintB);
    tuning.landscapeYawDeg = root.value("landscapeYawDeg", tuning.landscapeYawDeg);
    tuning.envMapMetallic = root.value("envMapMetallic", tuning.envMapMetallic);
    tuning.envMapRoughness = root.value("envMapRoughness", tuning.envMapRoughness);
    tuning.defaultRoughness = ReadInRange(root, "defaultRoughness", defaults.defaultRoughness, 0.0f, 1.0f);
    tuning.specularF0Scale = ReadInRange(root, "specularF0Scale", defaults.specularF0Scale, 0.0f, 1.0f);
    tuning.powerRoughnessScale = ReadInRange(root, "powerRoughnessScale", defaults.powerRoughnessScale, 0.0f, 2.0f);
    tuning.envSpecMetallic = ReadInRange(root, "envSpecMetallic", defaults.envSpecMetallic, 0.0f, 1.0f);
    tuning.envSpecRoughness = ReadInRange(root, "envSpecRoughness", defaults.envSpecRoughness, 0.0f, 1.0f);
    tuning.defaultMetallic = ReadInRange(root, "defaultMetallic", defaults.defaultMetallic, 0.0f, 1.0f);
    tuning.staticInstanceBatchResidencyCap = root.value(
        "staticInstanceBatchResidencyCap", tuning.staticInstanceBatchResidencyCap);
    tuning.camDistBandUpdateHz = root.value("camDistBandUpdateHz", tuning.camDistBandUpdateHz);
    tuning.shadowDistanceNear = root.value("shadowDistanceNear", tuning.shadowDistanceNear);
    tuning.shadowDistanceStandard = root.value("shadowDistanceStandard", tuning.shadowDistanceStandard);
    tuning.shadowDistanceFar = root.value("shadowDistanceFar", tuning.shadowDistanceFar);
    tuning.shadowFadeFraction = root.value("shadowFadeFraction", tuning.shadowFadeFraction);
    tuning.shadowSplitLambda = root.value("shadowSplitLambda", tuning.shadowSplitLambda);
    tuning.shadowCascadeBlendFraction =
        root.value("shadowCascadeBlendFraction", tuning.shadowCascadeBlendFraction);
    tuning.shadowFarCascadeSunAngleDeg = ReadInRange(
        root, "shadowFarCascadeSunAngleDeg", defaults.shadowFarCascadeSunAngleDeg, 0.0f, 180.0f);
    tuning.shadowThinCascadeCount = root.value("shadowThinCascadeCount", tuning.shadowThinCascadeCount);
    tuning.shadowStrength = root.value("shadowStrength", tuning.shadowStrength);
    tuning.shadowNormalOffsetTexels =
        root.value("shadowNormalOffsetTexels", tuning.shadowNormalOffsetTexels);
    tuning.shadowBlurRadiusM = root.value("shadowBlurRadiusM", tuning.shadowBlurRadiusM);
    tuning.shadowBlurRadiusNarrowM = root.value("shadowBlurRadiusNarrowM", tuning.shadowBlurRadiusNarrowM);
    tuning.shadowPcssLightSize = root.value("shadowPcssLightSize", tuning.shadowPcssLightSize);
    tuning.shadowPcssMaxRadiusM = root.value("shadowPcssMaxRadiusM", tuning.shadowPcssMaxRadiusM);
    tuning.shadowPcssBlockerBiasM = root.value("shadowPcssBlockerBiasM", tuning.shadowPcssBlockerBiasM);
    tuning.shadowDepthBias =root.value("shadowDepthBias", tuning.shadowDepthBias);
    tuning.shadowSlopeScaledDepthBias =
        root.value("shadowSlopeScaledDepthBias", tuning.shadowSlopeScaledDepthBias);
    tuning.shadowCullMode = root.value("shadowCullMode", tuning.shadowCullMode);
    tuning.shadowCasterExtensionM = root.value("shadowCasterExtensionM", tuning.shadowCasterExtensionM);
    tuning.shadowMinSunElevationDeg =
        root.value("shadowMinSunElevationDeg", tuning.shadowMinSunElevationDeg);
    tuning.shadowSunElevationFadeDeg =
        root.value("shadowSunElevationFadeDeg", tuning.shadowSunElevationFadeDeg);
    return tuning;
}

// 調整値の版の入れ物。関数内staticにするのは、main より前の静的初期化(シェーダの定義値)からも
// Embedded() が呼ばれるため(静的初期化の順序に依存しない)。
rs::core::TuningSlot<RenderTuning>& EmbeddedSlot() {
    static rs::core::TuningSlot<RenderTuning> slot(RenderTuning::ParseJson(std::string_view(
        reinterpret_cast<const char*>(rs::embedded::kRenderTuningJson), rs::embedded::kRenderTuningJsonSize)));
    return slot;
}

} // namespace

std::optional<RenderTuning> RenderTuning::TryParseJson(std::string_view json) {
    return rs::core::TryReadConfigJson<RenderTuning>(json, "RenderTuning", ReadTuning);
}

RenderTuning RenderTuning::ParseJson(std::string_view json) {
    std::optional<RenderTuning> parsed = TryParseJson(json);
    if (parsed) return *parsed;
    spdlog::warn("RenderTuning: 既定値を使います");
    return RenderTuning{};
}

const RenderTuning& RenderTuning::Embedded() { return EmbeddedSlot().Current(); }

void RenderTuning::PublishReloaded(RenderTuning tuning) { EmbeddedSlot().Publish(std::move(tuning)); }

} // namespace rs::render

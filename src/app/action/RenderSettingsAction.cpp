#include "RenderSettingsAction.h"

#include "../camera/CameraControl.h"
#include "../gpu/App.h"
#include "../settings/RuntimeSettingOverrides.h"
#include "../../render/postprocess/AoQuality.h"
#include "../../render/postprocess/PostProcessPlan.h"
#include "../../render/postprocess/TonemapMath.h"
#include "../../render/shadow/ShadowDistance.h"
#include "../../render/shadow/ShadowQuality.h"
#include "../settings/FpsLimitChoices.h"

namespace rs::app {

void ApplyRenderSettings(App& app, const UserSettingsData& settings) {
    app.SetAtmosphericPerspective(settings.atmosphericPerspective);
    app.SetHeightFog(settings.heightFog);
    app.SetShadowQuality(rs::render::ShadowQualityFromKey(settings.shadowQuality));
    app.SetShadowDistance(rs::render::ShadowDistanceFromKey(settings.shadowDistance));
    app.SetLensFlareSwitches(LensFlareSwitchesFrom(settings));
    app.SetPostProcessSettings(PostProcessSettingsFrom(settings));
    app.SetAmbientOcclusion(rs::render::AoQualityFromKey(settings.ambientOcclusion));
    app.SetVSync(settings.vsync);
    app.SetFpsLimit(settings.fpsLimit);
    app.SetTonemapMode(rs::render::TonemapModeFromEnvValue(settings.tonemapMode.c_str()));
}

void ApplyRuntimeSettingOverrides(App& app, const UserSettingsData& settings,
                                  const std::vector<std::string>& overrides) {
    UserSettingsData effective = settings;
    const RuntimeSettingOverrides parsed = ParseRuntimeSettingOverrides(overrides);
    if (parsed.antiAliasing) effective.antiAliasing = *parsed.antiAliasing;
    if (parsed.tonemap) effective.tonemapMode = *parsed.tonemap;
    if (parsed.atmosphericPerspective) effective.atmosphericPerspective = *parsed.atmosphericPerspective;
    if (parsed.heightFog) effective.heightFog = *parsed.heightFog;
    if (parsed.motionBlur) effective.motionBlur = *parsed.motionBlur;
    if (parsed.volumetricFog) effective.volumetricFog = *parsed.volumetricFog;
    // 空の描き方はシーンの環境で決まる。起動引数(--setting skyMode)の指定があるときだけ、開発用に上書きする。
    if (parsed.pluginSkyPreferred) app.SetPluginSkyPreferred(*parsed.pluginSkyPreferred);
    app.SetAtmosphericPerspective(effective.atmosphericPerspective);
    app.SetHeightFog(effective.heightFog);
    app.SetPostProcessSettings(PostProcessSettingsFrom(effective));
    app.SetTonemapMode(rs::render::TonemapModeFromEnvValue(effective.tonemapMode.c_str()));
}

UserSettingsData MakeSafeModeRenderSettings(const UserSettingsData& settings) {
    UserSettingsData safe = settings;
    safe.atmosphericPerspective = false;
    safe.heightFog = false;
    safe.shadowQuality = "none";
    safe.sunLensFlare = false;
    safe.sunWhiteout = false;
    safe.miscLensFlare = false;
    safe.miscParticle = false;
    safe.bloom = false;
    safe.motionBlur = false;
    safe.volumetricFog = false;
    safe.antiAliasing = "none";
    safe.ambientOcclusion = "none";
    return safe;
}

RenderSettingsAction::RenderSettingsAction(const RenderSettingsRefs& refs) : refs_(refs) {}

float RenderSettingsAction::RenderDistanceMin() {
    return kRenderDistanceMin;
}

float RenderSettingsAction::RenderDistanceMax() {
    return kRenderDistanceMax;
}

void RenderSettingsAction::SetRenderDistance(float distance) {
    refs_.camera.renderDistance = distance;
}

void RenderSettingsAction::SetAtmosphericPerspective(bool enabled) {
    refs_.userSettings.atmosphericPerspective = enabled;
    refs_.app.SetAtmosphericPerspective(enabled);
    SaveUserSettingsOrWarn(refs_.userSettings, "Failed to save user settings after atmospheric perspective selection.");
}

void RenderSettingsAction::SetHeightFog(bool enabled) {
    refs_.userSettings.heightFog = enabled;
    refs_.app.SetHeightFog(enabled);
    SaveUserSettingsOrWarn(refs_.userSettings, "Failed to save user settings after height fog selection.");
}

void RenderSettingsAction::SetTonemapMode(int mode) {
    const rs::render::TonemapMode tonemapMode = static_cast<rs::render::TonemapMode>(mode);
    refs_.userSettings.tonemapMode = rs::render::TonemapModeName(tonemapMode);
    refs_.app.SetTonemapMode(tonemapMode);
    SaveUserSettingsOrWarn(refs_.userSettings, "Failed to save user settings after tonemap selection.");
}

void RenderSettingsAction::SetShadowQualityIndex(int choiceIndex) {
    if (choiceIndex < 0 || choiceIndex >= static_cast<int>(rs::render::kShadowQualityChoices.size())) return;
    const rs::render::ShadowQuality quality = rs::render::kShadowQualityChoices[choiceIndex].quality;
    refs_.userSettings.shadowQuality = rs::render::ShadowQualityKey(quality);
    refs_.app.SetShadowQuality(quality);
    SaveUserSettingsOrWarn(refs_.userSettings, "Failed to save user settings after shadow quality selection.");
}

void RenderSettingsAction::SetShadowDistanceIndex(int choiceIndex) {
    if (choiceIndex < 0 || choiceIndex >= static_cast<int>(rs::render::kShadowDistanceChoices.size())) return;
    const rs::render::ShadowDistance distance = rs::render::kShadowDistanceChoices[choiceIndex].distance;
    refs_.userSettings.shadowDistance = rs::render::ShadowDistanceKey(distance);
    refs_.app.SetShadowDistance(distance);
    SaveUserSettingsOrWarn(refs_.userSettings, "Failed to save user settings after shadow distance selection.");
}

void RenderSettingsAction::ApplyLensFlareSwitches() {
    refs_.app.SetLensFlareSwitches(LensFlareSwitchesFrom(refs_.userSettings));
    SaveUserSettingsOrWarn(refs_.userSettings, "Failed to save user settings after lens flare selection.");
}

void RenderSettingsAction::SetSunLensFlare(bool enabled) {
    refs_.userSettings.sunLensFlare = enabled;
    ApplyLensFlareSwitches();
}

void RenderSettingsAction::SetSunWhiteout(bool enabled) {
    refs_.userSettings.sunWhiteout = enabled;
    ApplyLensFlareSwitches();
}

void RenderSettingsAction::SetMiscLensFlare(bool enabled) {
    refs_.userSettings.miscLensFlare = enabled;
    ApplyLensFlareSwitches();
}

void RenderSettingsAction::SetMiscParticle(bool enabled) {
    refs_.userSettings.miscParticle = enabled;
    SaveUserSettingsOrWarn(refs_.userSettings, "Failed to save user settings after particle selection.");
}

void RenderSettingsAction::SetBloom(bool enabled) {
    refs_.userSettings.bloom = enabled;
    refs_.app.SetPostProcessSettings(PostProcessSettingsFrom(refs_.userSettings));
    SaveUserSettingsOrWarn(refs_.userSettings, "Failed to save user settings after bloom selection.");
}

void RenderSettingsAction::SetVolumetricFog(bool enabled) {
    refs_.userSettings.volumetricFog = enabled;
    refs_.app.SetPostProcessSettings(PostProcessSettingsFrom(refs_.userSettings));
    SaveUserSettingsOrWarn(refs_.userSettings, "Failed to save user settings after volumetric fog selection.");
}

void RenderSettingsAction::SetMotionBlur(bool enabled) {
    refs_.userSettings.motionBlur = enabled;
    refs_.app.SetPostProcessSettings(PostProcessSettingsFrom(refs_.userSettings));
    SaveUserSettingsOrWarn(refs_.userSettings, "Failed to save user settings after motion blur selection.");
}

void RenderSettingsAction::SetAntiAliasingIndex(int choiceIndex) {
    if (choiceIndex < 0 || choiceIndex >= static_cast<int>(rs::render::kAntiAliasingChoices.size())) return;
    refs_.userSettings.antiAliasing = rs::render::kAntiAliasingChoices[choiceIndex].key;
    refs_.app.SetPostProcessSettings(PostProcessSettingsFrom(refs_.userSettings));
    SaveUserSettingsOrWarn(refs_.userSettings, "Failed to save user settings after anti-aliasing selection.");
}

void RenderSettingsAction::SetAmbientOcclusionIndex(int choiceIndex) {
    if (choiceIndex < 0 || choiceIndex >= static_cast<int>(rs::render::kAoQualityChoices.size())) return;
    const rs::render::AoQuality quality = rs::render::kAoQualityChoices[choiceIndex].quality;
    refs_.userSettings.ambientOcclusion = rs::render::AoQualityKey(quality);
    refs_.app.SetAmbientOcclusion(quality);
    SaveUserSettingsOrWarn(refs_.userSettings, "Failed to save user settings after ambient occlusion selection.");
}

void RenderSettingsAction::SetVSync(bool enabled) {
    refs_.userSettings.vsync = enabled;
    refs_.app.SetVSync(enabled);
    SaveUserSettingsOrWarn(refs_.userSettings, "Failed to save user settings after VSync selection.");
}

void RenderSettingsAction::SetFpsLimitIndex(int choiceIndex) {
    if (choiceIndex < 0 || choiceIndex >= static_cast<int>(kFpsLimitChoices.size())) return;
    const int fpsLimit = kFpsLimitChoices[choiceIndex];
    refs_.userSettings.fpsLimit = fpsLimit;
    refs_.app.SetFpsLimit(fpsLimit);
    SaveUserSettingsOrWarn(refs_.userSettings, "Failed to save user settings after FPS limit selection.");
}

RenderSettingsViewModel RenderSettingsAction::ViewModel() const {
    RenderSettingsViewModel viewModel;
    viewModel.renderDistance = refs_.camera.renderDistance;
    viewModel.atmosphericPerspective = refs_.userSettings.atmosphericPerspective;
    viewModel.heightFog = refs_.userSettings.heightFog;
    viewModel.tonemapMode = static_cast<int>(rs::render::TonemapModeFromEnvValue(refs_.userSettings.tonemapMode.c_str()));
    const rs::render::ShadowQuality quality = rs::render::ShadowQualityFromKey(refs_.userSettings.shadowQuality);
    for (std::size_t i = 0; i < rs::render::kShadowQualityChoices.size(); ++i) {
        if (rs::render::kShadowQualityChoices[i].quality == quality) viewModel.shadowQualityIndex = static_cast<int>(i);
    }
    const rs::render::ShadowDistance distance = rs::render::ShadowDistanceFromKey(refs_.userSettings.shadowDistance);
    for (std::size_t i = 0; i < rs::render::kShadowDistanceChoices.size(); ++i) {
        if (rs::render::kShadowDistanceChoices[i].distance == distance) viewModel.shadowDistanceIndex = static_cast<int>(i);
    }
    viewModel.sunLensFlare = refs_.userSettings.sunLensFlare;
    viewModel.sunWhiteout = refs_.userSettings.sunWhiteout;
    viewModel.miscLensFlare = refs_.userSettings.miscLensFlare;
    viewModel.miscParticle = refs_.userSettings.miscParticle;
    viewModel.bloom = refs_.userSettings.bloom;
    viewModel.motionBlur = refs_.userSettings.motionBlur;
    viewModel.volumetricFog = refs_.userSettings.volumetricFog;
    const rs::render::AntiAliasing aa = rs::render::AntiAliasingFromKey(refs_.userSettings.antiAliasing);
    for (std::size_t i = 0; i < rs::render::kAntiAliasingChoices.size(); ++i) {
        if (rs::render::kAntiAliasingChoices[i].aa == aa) viewModel.antiAliasingIndex = static_cast<int>(i);
    }
    const rs::render::AoQuality ao = rs::render::AoQualityFromKey(refs_.userSettings.ambientOcclusion);
    for (std::size_t i = 0; i < rs::render::kAoQualityChoices.size(); ++i) {
        if (rs::render::kAoQualityChoices[i].quality == ao) viewModel.ambientOcclusionIndex = static_cast<int>(i);
    }
    viewModel.vsync = refs_.userSettings.vsync;
    const int fpsLimit = NormalizeFpsLimit(refs_.userSettings.fpsLimit);
    for (std::size_t i = 0; i < kFpsLimitChoices.size(); ++i) {
        if (kFpsLimitChoices[i] == fpsLimit) viewModel.fpsLimitIndex = static_cast<int>(i);
    }
    return viewModel;
}

} // namespace rs::app

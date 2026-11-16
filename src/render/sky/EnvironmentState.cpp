#include "EnvironmentState.h"

#include "../../platform/EnvironmentVariable.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <numbers>
#include <string>

#include "../../platform/text/AsciiCase.h"
#include "../postprocess/TonemapMath.h"
#include "../resource/RenderTuning.h"
#include "AtmosphereParams.h"
#include "IblParams.h"

namespace rs::render {

namespace {

std::string Lowered(const char* value) {
    return rs::platform_text::ToAsciiLower(value);
}

// 水平成分がこれより短い方向は、方位が決まらないものとして回転しない。
constexpr float kYawMinHorizontalLength = 1e-3f;

// 月の光を使うか。太陽が高いときは月の寄与が見えないので0にする(昼にLUT・IBLを作り直さないため)。
bool MoonActive(const MoonInput& moon, float sunElevationSin) {
    if (moon.brightness <= 0.0f) return false;
    const double sunElevationDeg =
        std::asin(std::clamp(static_cast<double>(sunElevationSin), -1.0, 1.0)) * 180.0 / std::numbers::pi;
    return sunElevationDeg <= atmosphere::kMoonActiveMaxSunElevationDeg;
}

} // namespace

YawRotation MakeYawAlignment(const float sunToSun[3], const float photoSun[3]) {
    const float sunLength = std::hypot(sunToSun[0], sunToSun[2]);
    const float photoLength = std::hypot(photoSun[0], photoSun[2]);
    if (sunLength < kYawMinHorizontalLength || photoLength < kYawMinHorizontalLength) return YawRotation{};
    // 水平成分を複素数 x+iz とみなすと、RotateYaw は (cos - i sin) を掛ける操作。
    // (cos - i sin) = photo * conj(sun) を正規化したものが、sun を photo へ重ねる。
    const float sx = sunToSun[0] / sunLength;
    const float sz = sunToSun[2] / sunLength;
    const float px = photoSun[0] / photoLength;
    const float pz = photoSun[2] / photoLength;
    YawRotation rotation;
    rotation.cos = px * sx + pz * sz;
    rotation.sin = px * sz - pz * sx;
    return rotation;
}

bool StandardSkyPhotoFromEnvValue(const char* value) {
    if (value == nullptr) return true;
    return Lowered(value) != "off";
}

bool StandardSkyPhotoFullSphereFromEnvValue(const char* value) {
    return value == nullptr || Lowered(value) != "sky";
}

bool IblEnabledFromEnvValue(const char* value) {
    if (value == nullptr) return true;
    return Lowered(value) != "off";
}

bool AerialPerspectiveEnabledFromEnvValue(const char* value) {
    if (value == nullptr) return true;
    return Lowered(value) != "off";
}

bool HeightFogEnabledFromEnvValue(const char* value) {
    if (value == nullptr) return true;
    return Lowered(value) != "off";
}

DebugIblMode DebugIblModeFromEnvValue(const char* value) {
    if (value == nullptr) return DebugIblMode::Off;
    const std::string text = Lowered(value);
    if (text == "ambient") return DebugIblMode::Ambient;
    if (text == "specular") return DebugIblMode::Specular;
    if (text == "suncolor") return DebugIblMode::SunColor;
    return DebugIblMode::Off;
}

bool PluginSkyRequestedFromEnvValue(const char* value, bool fallback) {
    if (value == nullptr) return fallback;
    const std::string text = Lowered(value);
    if (text == "plugin") return true;
    if (text == "standard") return false;
    return fallback;
}

EnvironmentState MakeEnvironmentState(const RenderTuning& tuning, const EnvironmentSrvIndices& srv,
                                      bool iblReady, const char* iblEnvValue, const char* debugEnvValue,
                                      const PluginSkyInput& pluginSky, bool pluginSkyRequested,
                                      const char* standardSkyPhotoEnvValue, const SunToSunInput& sunToSun,
                                      const MoonInput& moon) {
    EnvironmentState state;
    state.iblEnabled = iblReady && IblEnabledFromEnvValue(iblEnvValue);
    state.pluginSky = state.iblEnabled && pluginSky.available && pluginSkyRequested;
    if (state.pluginSky) {
        state.pluginSrv = pluginSky.srv;
        state.pluginSpecularMipCount = pluginSky.specularMipCount;
        for (int i = 0; i < 3; ++i) {
            // Lightingが無いEnvは、標準の空の固定の環境光(ambientIntensity)・白の直射光と同じにする。
            state.pluginDirectional[i] = pluginSky.hasLighting ? SrgbToLinear(pluginSky.directional[i]) : 1.0f;
            state.pluginAmbient[i] = pluginSky.hasLighting ? SrgbToLinear(pluginSky.ambient[i])
                                                           : tuning.ambientIntensity;
            state.pluginMeshLightScale[i] = pluginSky.hasLighting ? SrgbToLinear(pluginSky.meshLightScale[i]) : 1.0f;
            state.pluginLandscapeLight[i] = pluginSky.hasLighting ? pluginSky.ambient[i] : 1.0f;
        }
    }
    state.skyRadianceScale = state.pluginSky ? tuning.PluginSkyRadianceScale() : tuning.skyRadianceScale;
    state.adaptation = state.iblEnabled && !state.pluginSky;
    state.photoSky = state.iblEnabled && !state.pluginSky && pluginSky.available && pluginSky.photoStatsOk &&
                     StandardSkyPhotoFromEnvValue(standardSkyPhotoEnvValue);
    state.photoFullSphere = state.photoSky && StandardSkyPhotoFullSphereFromEnvValue(standardSkyPhotoEnvValue);
    if (state.photoSky) {
        state.photoSunFound = pluginSky.photoSunFound;
        for (int i = 0; i < 3; ++i) {
            state.photoSkyMean[i] = pluginSky.photoSkyMean[i];
            state.photoSunDirection[i] = pluginSky.photoSunDirection[i];
        }
        if (state.photoSunFound) {
            const float toSun[3] = {sunToSun.x, sunToSun.y, sunToSun.z};
            state.photoYaw = MakeYawAlignment(toSun, state.photoSunDirection);
        }
    }
    const float moonToMoon[3] = {moon.x, moon.y, moon.z};
    const float moonLength = std::sqrt(moon.x * moon.x + moon.y * moon.y + moon.z * moon.z);
    if (!state.pluginSky && moon.brightness > 0.0f && moonLength > 0.0f) {
        state.moonDiskVisible = true;
        for (int i = 0; i < 3; ++i) state.moonDiskDirection[i] = moonToMoon[i] / moonLength;
    }
    if (state.iblEnabled && !state.pluginSky && moonLength > 0.0f && MoonActive(moon, sunToSun.y)) {
        for (int i = 0; i < 3; ++i) state.moonDirection[i] = state.moonDiskDirection[i];
        state.moonRatio = atmosphere::kFullMoonToSunIrradianceRatio * moon.brightness;
    }
    state.debugMode = DebugIblModeFromEnvValue(debugEnvValue);
    state.srv = srv;
    state.specularMipCount = ibl::kIblSpecularMipCount;
    state.iblDiffuseIntensity = tuning.iblDiffuseIntensity;
    // 写真を全方向の環境に使うときだけ、反射に photoSpecularGain を掛ける(写真なしの環境には掛けない)。
    state.iblSpecularIntensity =
        tuning.iblSpecularIntensity * (state.photoFullSphere ? tuning.photoSpecularGain : 1.0f);
    state.pluginSkyDiffuseIntensity = tuning.pluginSkyDiffuseIntensity;
    state.pluginSkySpecularIntensity = tuning.pluginSkySpecularIntensity;
    state.iblSpecularMipScale = tuning.iblSpecularMipScale;
    state.envMapMetallic = tuning.envMapMetallic;
    state.envMapRoughness = tuning.envMapRoughness;
    return state;
}

namespace {
const char* IblEnvValue() {
    static const char* const kIblEnv = rs::platform::ReadEnvironmentVariable("RS_IBL");
    return kIblEnv;
}
} // namespace

EnvironmentState BuildEnvironmentState(const RenderTuning& tuning, const EnvironmentSrvIndices& srv,
                                       bool iblReady, const PluginSkyInput& pluginSky, bool pluginSkyPreferred,
                                       const SunToSunInput& sunToSun, const MoonInput& moon) {
    const char* const kIblEnv = IblEnvValue();
    static const char* const kDebugEnv = rs::platform::ReadEnvironmentVariable("RS_DEBUG_IBL");
    static const char* const kStandardSkyPhotoEnv = rs::platform::ReadEnvironmentVariable("RS_STANDARD_SKY_PHOTO");
    return MakeEnvironmentState(tuning, srv, iblReady, kIblEnv, kDebugEnv, pluginSky,
                                pluginSkyPreferred,
                                kStandardSkyPhotoEnv, sunToSun, moon);
}

} // namespace rs::render

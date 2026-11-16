#include "PluginSkyLighting.h"

#include "../../plugins/schema/EnvLighting.h"

namespace rs::app {

rs::render::PluginSkyInput MakePluginSkyInput(const ActiveEnv& activeEnv, const EnvCubeController& cubes,
                                              double sunAltSin) {
    rs::render::PluginSkyInput input;
    input.available = cubes.PluginSkyAvailable();
    const rs::render::EnvPhotoStats& stats = cubes.PhotoStats();
    input.photoStatsOk = stats.ok;
    input.photoSunFound = stats.sunFound;
    for (int i = 0; i < 3; ++i) {
        input.photoSkyMean[i] = static_cast<float>(stats.skyMean[i]);
        input.photoSunDirection[i] = static_cast<float>(stats.sunDirection[i]);
    }
    const rs::legacy_pi::EnvDefinition* env = activeEnv.Definition();
    if (env == nullptr) return input;

    const rs::legacy_pi::EnvLightingValue lighting =
        rs::legacy_pi::EvaluateEnvLighting(*env, static_cast<float>(sunAltSin));
    input.hasLighting = lighting.hasLighting;
    if (lighting.hasLighting) {
        input.directional[0] = lighting.directional.r;
        input.directional[1] = lighting.directional.g;
        input.directional[2] = lighting.directional.b;
        input.ambient[0] = lighting.ambient.r;
        input.ambient[1] = lighting.ambient.g;
        input.ambient[2] = lighting.ambient.b;
    }
    const rs::legacy_pi::PiColor scale = rs::legacy_pi::EvaluateEnvMeshLightScale(lighting);
    input.meshLightScale[0] = scale.r;
    input.meshLightScale[1] = scale.g;
    input.meshLightScale[2] = scale.b;
    return input;
}

} // namespace rs::app

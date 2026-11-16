#include "EnvSchema.h"

#include <string>

#include "LensFlareSchema.h"
#include "SchemaValueRead.h"
// 値の取り出し(GetString/GetDouble)は.rs2読込と同じヘルパーを共用する(header-onlyで依存はヘッダだけ)。
#include "../layout/PiValueAccessors.h"

namespace rs::legacy_pi {

namespace {

using rs::legacy_layout::GetDouble;
using rs::legacy_layout::GetString;

void ReadLighting(const PiBlock& lighting, Diagnostics& diag, EnvDefinition& env) {
    env.nightThreshold = GetDouble(lighting, "NightThreshold", 0.0);
    env.hasShadowColor = ReadColor(lighting, "ShadowColor", diag, env.shadowColor);
    for (const PiBlock* set : lighting.Children("Set")) {
        EnvLightingSet entry;
        entry.sunAlt = GetDouble(*set, "SunAlt", 0.0);
        ReadColor(*set, "Directional", diag, entry.directional);
        ReadColor(*set, "Ambient", diag, entry.ambient);
        ReadColor(*set, "SkyColor", diag, entry.skyColor);
        env.lightingSets.push_back(entry);
    }
}

void ReadSun(const PiBlock& sun, Diagnostics& diag, EnvDefinition& env) {
    env.sunModelFileName = GetString(sun, "ModelFileName");
    env.sunModelScale = GetDouble(sun, "ModelScale", 1.0);
    if (const PiBlock* lighting = sun.FirstChild("Lighting")) ReadLighting(*lighting, diag, env);
    if (const PiBlock* whiteout = sun.FirstChild("Whiteout")) {
        env.whiteout.present = true;
        env.whiteout.startAngle = GetDouble(*whiteout, "StartAngle", 0.0);
        ReadColor(*whiteout, "Color", diag, env.whiteout.color);
    }
    if (const PiBlock* lensFlare = sun.FirstChild("LensFlare")) env.lensFlare = ReadLensFlare(*lensFlare, diag);
}

} // namespace

EnvDefinition BuildEnvDefinition(const PiDocument& doc, Diagnostics& diag) {
    EnvDefinition env;
    bool moonRead = false;
    for (const PiBlock& node : doc.nodes) {
        if (node.name == "EnvInfo") {
            env.hasLatitude = ReadNumber(node, "Latitude", diag, env.latitudeDeg);
            env.envMapTexFileName = GetString(node, "EnvMapTexFileName");
            env.diffuseIrradianceTexFileName = GetString(node, "DiffuseIrradianceTexFileName");
            env.specularIrradianceTexFileName = GetString(node, "SpecularIrradianceTexFileName");
            env.brdfLutTexFileName = GetString(node, "BRDFLUTTexFileName");
        } else if (node.name == "Landscape") {
            env.landscapeModelFileName = GetString(node, "ModelFileName");
            env.landscapeModelScale = GetDouble(node, "ModelScale", 1.0);
        } else if (node.name == "Moon" && !moonRead) {
            moonRead = true;
            ReadNumber(node, "RevolutionPeriod", diag, env.moonRevolutionPeriodDays);
            ReadNumber(node, "InitialPhase", diag, env.moonInitialPhaseDays);
            env.moonModelFileName = GetString(node, "ModelFileName");
            env.moonModelScale = GetDouble(node, "ModelScale", 1.0);
        } else if (node.name == "Sun") {
            ReadSun(node, diag, env);
        }
    }
    return env;
}

double ResolveSolarLatitudeDeg(const EnvDefinition* env, double fallbackDeg) {
    return (env != nullptr && env->hasLatitude) ? env->latitudeDeg : fallbackDeg;
}

} // namespace rs::legacy_pi

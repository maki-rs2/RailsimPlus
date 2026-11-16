#include "EnvLighting.h"

#include <algorithm>
#include <vector>

namespace rs::legacy_pi {

namespace {

PiColor MixColor(const PiColor& a, const PiColor& b, float weightA) {
    const float weightB = 1.0f - weightA;
    PiColor out;
    out.r = weightA * a.r + weightB * b.r;
    out.g = weightA * a.g + weightB * b.g;
    out.b = weightA * a.b + weightB * b.b;
    out.a = weightA * a.a + weightB * b.a;
    return out;
}

EnvLightingValue FromSet(const EnvLightingSet& set) {
    EnvLightingValue value;
    value.hasLighting = true;
    value.directional = set.directional;
    value.ambient = set.ambient;
    value.skyColor = set.skyColor;
    return value;
}

} // namespace

EnvLightingValue EvaluateEnvLighting(const EnvDefinition& env, float sunAltSin) {
    if (env.lightingSets.empty()) return EnvLightingValue{};

    // RailSim II は読込時に SunAlt の昇順へ並べ替える(同値の並びは宣言順のまま)。
    std::vector<EnvLightingSet> sets = env.lightingSets;
    std::stable_sort(sets.begin(), sets.end(),
                     [](const EnvLightingSet& a, const EnvLightingSet& b) { return a.sunAlt < b.sunAlt; });

    const double alt = static_cast<double>(sunAltSin);
    for (std::size_t i = 0; i < sets.size(); ++i) {
        const bool isLast = (i + 1 == sets.size());
        if (alt <= sets[i].sunAlt || isLast) return FromSet(sets[i]);
        const EnvLightingSet& upper = sets[i + 1];
        if (alt <= upper.sunAlt) {
            const EnvLightingSet& lower = sets[i];
            const float weightLower = static_cast<float>((upper.sunAlt - alt) / (upper.sunAlt - lower.sunAlt));
            EnvLightingValue value;
            value.hasLighting = true;
            value.directional = MixColor(lower.directional, upper.directional, weightLower);
            value.ambient = MixColor(lower.ambient, upper.ambient, weightLower);
            value.skyColor = MixColor(lower.skyColor, upper.skyColor, weightLower);
            return value;
        }
    }
    return FromSet(sets.back());
}

PiColor EvaluateEnvMeshLightScale(const EnvLightingValue& lighting) {
    PiColor scale;
    scale.r = scale.g = scale.b = 1.0f;
    if (!lighting.hasLighting) return scale;
    scale.r = std::max(lighting.directional.r, lighting.ambient.r);
    scale.g = std::max(lighting.directional.g, lighting.ambient.g);
    scale.b = std::max(lighting.directional.b, lighting.ambient.b);
    return scale;
}

} // namespace rs::legacy_pi

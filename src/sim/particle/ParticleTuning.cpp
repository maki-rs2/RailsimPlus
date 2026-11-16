#include "ParticleTuning.h"

#include "../../core/ConfigJson.h"
#include "EmbeddedParticleTuning.h"

#include <cmath>
#include <stdexcept>

namespace rs::sim {

const ParticleTuning& ParticleTuning::Embedded() {
    static const ParticleTuning kTuning = core::ReadEmbeddedConfig<ParticleTuning>(
        embedded::kParticleTuningJson, embedded::kParticleTuningJsonSize, "ParticleTuning",
        [](const nlohmann::json& root) {
            ParticleTuning tuning;
            const std::int64_t maxParticles = root.value("maxParticles", static_cast<std::int64_t>(tuning.maxParticles));
            const std::int64_t maxSubsteps = root.value("maxSubsteps", static_cast<std::int64_t>(tuning.maxSubsteps));
            tuning.maxStepSeconds = root.value("maxStepSeconds", tuning.maxStepSeconds);
            tuning.smoothingSeconds = root.value("smoothingSeconds", tuning.smoothingSeconds);
            tuning.noiseSpatialScale = root.value("noiseSpatialScale", tuning.noiseSpatialScale);
            tuning.noiseTemporalScale = root.value("noiseTemporalScale", tuning.noiseTemporalScale);
            tuning.maxAngularSpeedRadiansPerSecond = root.value(
                "maxAngularSpeedRadiansPerSecond", tuning.maxAngularSpeedRadiansPerSecond);
            if (maxParticles < 0 || maxSubsteps <= 0 || !std::isfinite(tuning.maxStepSeconds) ||
                tuning.maxStepSeconds <= 0.0 || !std::isfinite(tuning.smoothingSeconds) ||
                tuning.smoothingSeconds < 0.0 || !std::isfinite(tuning.noiseSpatialScale) ||
                !std::isfinite(tuning.noiseTemporalScale) ||
                !std::isfinite(tuning.maxAngularSpeedRadiansPerSecond) || tuning.maxAngularSpeedRadiansPerSecond < 0.0) {
                throw std::invalid_argument("粒子の調整値が範囲外です");
            }
            tuning.maxParticles = static_cast<std::size_t>(maxParticles);
            tuning.maxSubsteps = static_cast<std::size_t>(maxSubsteps);
            return tuning;
        });
    return kTuning;
}

} // namespace rs::sim

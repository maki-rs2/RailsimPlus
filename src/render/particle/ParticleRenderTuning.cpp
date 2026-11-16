#include "ParticleRenderTuning.h"

#include "../../core/ConfigJson.h"
#include "EmbeddedParticleRenderTuning.h"

#include <cmath>
#include <stdexcept>

namespace rs::render {

const ParticleRenderTuning& ParticleRenderTuning::Embedded() {
    static const ParticleRenderTuning kTuning = core::ReadEmbeddedConfig<ParticleRenderTuning>(
        embedded::kParticleRenderTuningJson, embedded::kParticleRenderTuningJsonSize, "ParticleRenderTuning",
        [](const nlohmann::json& root) {
            ParticleRenderTuning tuning;
            tuning.stretchSeconds = root.value("stretchSeconds", tuning.stretchSeconds);
            tuning.maxStretchRatio = root.value("maxStretchRatio", tuning.maxStretchRatio);
            tuning.softDepthMeters = root.value("softDepthMeters", tuning.softDepthMeters);
            tuning.nearFadeStartMeters = root.value("nearFadeStartMeters", tuning.nearFadeStartMeters);
            tuning.nearFadeEndMeters = root.value("nearFadeEndMeters", tuning.nearFadeEndMeters);
            tuning.ambientScale = root.value("ambientScale", tuning.ambientScale);
            tuning.sunScale = root.value("sunScale", tuning.sunScale);
            if (!std::isfinite(tuning.stretchSeconds) || tuning.stretchSeconds < 0.0 ||
                !std::isfinite(tuning.maxStretchRatio) || tuning.maxStretchRatio < 1.0 ||
                !std::isfinite(tuning.softDepthMeters) || tuning.softDepthMeters <= 0.0 ||
                !std::isfinite(tuning.nearFadeStartMeters) || tuning.nearFadeStartMeters < 0.0 ||
                !std::isfinite(tuning.nearFadeEndMeters) || tuning.nearFadeEndMeters <= tuning.nearFadeStartMeters ||
                !std::isfinite(tuning.ambientScale) || tuning.ambientScale < 0.0 ||
                !std::isfinite(tuning.sunScale) || tuning.sunScale < 0.0) {
                throw std::invalid_argument("粒子の描画調整値が範囲外です");
            }
            return tuning;
        });
    return kTuning;
}

} // namespace rs::render

#include "AudioTuning.h"

#include "../../core/ConfigJson.h"

#include <nlohmann/json.hpp>

#include "EmbeddedAudioTuning.h"

namespace rs::sim {

namespace {

AudioTuning ParseEmbedded() {
    return rs::core::ReadEmbeddedConfig<AudioTuning>(
        rs::embedded::kAudioTuningJson, rs::embedded::kAudioTuningJsonSize, "AudioTuning",
        [](const nlohmann::json& root) {
            AudioTuning tuning;
            tuning.placedSoundReferenceDistanceMeters = root.value(
                "placedSoundReferenceDistanceMeters", tuning.placedSoundReferenceDistanceMeters);
            tuning.placedSoundCullRadiusMeters = root.value(
                "placedSoundCullRadiusMeters", tuning.placedSoundCullRadiusMeters);
            tuning.listenerTeleportThresholdMeters = root.value(
                "listenerTeleportThresholdMeters", tuning.listenerTeleportThresholdMeters);
            tuning.dopplerFactor = root.value("dopplerFactor", tuning.dopplerFactor);
            tuning.listenerVelocityEnabled = root.value("listenerVelocityEnabled", tuning.listenerVelocityEnabled);
            tuning.velocitySmoothingSeconds = root.value("velocitySmoothingSeconds", tuning.velocitySmoothingSeconds);
            tuning.masterVolume = root.value("masterVolume", tuning.masterVolume);
            tuning.wheelSoundMaxTotalVoices = root.value("wheelSoundMaxTotalVoices", tuning.wheelSoundMaxTotalVoices);
            return tuning;
        });
}

} // namespace

const AudioTuning& AudioTuning::Embedded() {
    static const AudioTuning kTuning = ParseEmbedded();
    return kTuning;
}

} // namespace rs::sim

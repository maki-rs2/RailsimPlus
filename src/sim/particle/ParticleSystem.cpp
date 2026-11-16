#include "ParticleSystem.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_set>
#include <vector>

namespace rs::sim {

std::size_t ParticleEmitterKeyHash::operator()(const ParticleEmitterKey& key) const noexcept {
    const std::size_t slot = std::hash<std::size_t>{}(key.slotIndex);
    const std::size_t generation = std::hash<std::uint64_t>{}(key.generation);
    return slot ^ (generation + 0x9e3779b9U + (slot << 6U) + (slot >> 2U));
}

ParticleSystem::ParticleSystem(std::uint64_t seed, const ParticleTuning& tuning)
    : tuning_(tuning), pool_(tuning.maxParticles), random_(seed), noise_(seed) {
    if (!std::isfinite(tuning_.maxStepSeconds) || tuning_.maxStepSeconds <= 0.0 || tuning_.maxSubsteps == 0 ||
        !std::isfinite(tuning_.smoothingSeconds) || tuning_.smoothingSeconds < 0.0) {
        throw std::invalid_argument("粒子の時間調整値が範囲外です");
    }
}

void ParticleSystem::Emit(const ParticleEmitRequest& request, ParticleEmitterState& state,
                          const ParticleEmissionFrame& frame, double frameDtSeconds, double elapsedSeconds,
                          double stepSeconds, const ParticleEnvironment& environment) {
    const ParticleEmissionTimes schedule = ParticleEmission::Schedule(
        state.credit, frame.rate, stepSeconds, pool_.Capacity() - pool_.Size());
    for (std::size_t index = 0; index < schedule.count; ++index) {
        const double offsetSeconds = std::min(stepSeconds,
            schedule.firstSeconds + static_cast<double>(index) * schedule.intervalSeconds);
        const double fraction = (elapsedSeconds + offsetSeconds) / frameDtSeconds;
        const core::DVec3 position = core::Add(frame.previousPosition,
            core::Scale(core::Sub(frame.currentPosition, frame.previousPosition), fraction));
        ParticleState particle = ParticleEmission::Create(request.params, request.worldAxes, position,
            frame.velocity, random_, tuning_.maxAngularSpeedRadiansPerSecond);
        if (ParticleStep::Advance(particle, stepSeconds - offsetSeconds, timeSeconds_ + offsetSeconds,
                                  environment, noise_, tuning_)) {
            pool_.Add(particle);
        }
    }
}

void ParticleSystem::Update(std::span<const ParticleEmitRequest> requests, double realDtSeconds,
                            double timeScale, const ParticleEnvironment& environment) {
    double frameDtSeconds = realDtSeconds * timeScale;
    if (!std::isfinite(frameDtSeconds) || realDtSeconds <= 0.0 || timeScale <= 0.0) frameDtSeconds = 0.0;
    struct FrameEmitter {
        const ParticleEmitRequest* request;
        ParticleEmitterState* state;
        ParticleEmissionFrame emission;
    };
    std::vector<FrameEmitter> frames;
    frames.reserve(requests.size());
    std::unordered_set<ParticleEmitterKey, ParticleEmitterKeyHash> currentKeys;
    currentKeys.reserve(requests.size());
    for (const ParticleEmitRequest& request : requests) {
        if (!currentKeys.insert(request.key).second) continue;
        ParticleEmitterState& state = emitters_[request.key];
        frames.push_back({&request, &state, ParticleEmission::Prepare(
            state, request.params, request.worldPosition, frameDtSeconds, tuning_.smoothingSeconds)});
    }
    std::erase_if(emitters_, [&currentKeys](const std::pair<const ParticleEmitterKey, ParticleEmitterState>& entry) {
        return !currentKeys.contains(entry.first);
    });
    if (frameDtSeconds <= 0.0) return;

    double elapsedSeconds = 0.0;
    for (std::size_t substep = 0; substep < tuning_.maxSubsteps && elapsedSeconds < frameDtSeconds; ++substep) {
        const double stepSeconds = std::min(tuning_.maxStepSeconds, frameDtSeconds - elapsedSeconds);
        ParticleStep::Update(pool_, stepSeconds, timeSeconds_, environment, noise_, tuning_);
        for (const FrameEmitter& frame : frames) {
            Emit(*frame.request, *frame.state, frame.emission, frameDtSeconds, elapsedSeconds, stepSeconds, environment);
        }
        elapsedSeconds += stepSeconds;
        timeSeconds_ += stepSeconds;
    }
    const double skippedSeconds = std::max(0.0, frameDtSeconds - elapsedSeconds);
    ParticleStep::AgeOnly(pool_, skippedSeconds);
    timeSeconds_ += skippedSeconds;
}

} // namespace rs::sim

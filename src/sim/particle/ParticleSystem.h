#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>

#include "ParticleEmission.h"
#include "ParticleStep.h"

namespace rs::sim {

// slotIndexはエミッタのスロット。同じスロットの再利用はgenerationで区別する。
struct ParticleEmitterKey {
    std::size_t slotIndex = 0;
    std::uint64_t generation = 0;
    bool operator==(const ParticleEmitterKey&) const = default;
};

struct ParticleEmitterKeyHash {
    std::size_t operator()(const ParticleEmitterKey& key) const noexcept;
};

struct ParticleEmitRequest {
    ParticleEmitterKey key;
    ParticleEmitterParams params;
    // SourceCoordを含めて解決した放出点。軸には平行移動や拡縮を含めない。
    core::DVec3 worldPosition;
    std::array<core::DVec3, 3> worldAxes = {{{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}}};
};

class ParticleSystem {
public:
    explicit ParticleSystem(std::uint64_t seed = 0, const ParticleTuning& tuning = ParticleTuning::Embedded());
    // 要求のキーは一覧内で一意とする。新しいキーは位置を記録し、次の更新から放出する。
    void Update(std::span<const ParticleEmitRequest> requests, double realDtSeconds, double timeScale,
                const ParticleEnvironment& environment = {});
    const ParticlePool& Pool() const { return pool_; }
    std::size_t EmitterCount() const { return emitters_.size(); }

private:
    void Emit(const ParticleEmitRequest& request, ParticleEmitterState& state, const ParticleEmissionFrame& frame,
              double frameDtSeconds, double elapsedSeconds, double stepSeconds, const ParticleEnvironment& environment);

    ParticleTuning tuning_;
    ParticlePool pool_;
    ParticleRandom random_;
    ParticleNoise noise_;
    double timeSeconds_ = 0.0;
    std::unordered_map<ParticleEmitterKey, ParticleEmitterState, ParticleEmitterKeyHash> emitters_;
};

} // namespace rs::sim

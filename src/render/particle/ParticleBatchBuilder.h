#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "../../sim/particle/ParticlePool.h"
#include "ParticleRenderTuning.h"

namespace rs::render {

struct ParticleGpuInstance {
    float position[3] = {};
    float radius = 0.0f;
    float color[4] = {};
    float angleRadians = 0.0f;
    std::uint32_t textureSlot = 0;
    std::uint32_t blendMode = 0;
    float padding = 0.0f;
    float velocity[3] = {};
    float depth = 0.0f;
    // 画面平面へ射影した速度の単位ベクトル(ワールド空間)。伸ばさない場合はゼロ。
    float stretchDirection[3] = {};
    float length = 0.0f;
};
static_assert(sizeof(ParticleGpuInstance) == 80);

// プールの参照は、フレームの記録が終わるまで有効であること。
struct ParticleFrameInput {
    const rs::sim::ParticlePool& pool;
    rs::core::DVec3 cameraPosition;
    rs::core::DVec3 cameraForward;
    std::size_t maxParticles;
};

class ParticleBatchBuilder {
public:
    // 上限で捨てた数を返す。作業領域と出力の容量はフレーム間で再利用する。
    std::size_t Build(const rs::sim::ParticlePool& pool, const rs::core::DVec3& cameraPosition,
                      const rs::core::DVec3& cameraForward,
                      const std::function<bool(std::uint32_t)>& hasTexture,
                      std::size_t maxParticles,
                      const ParticleRenderTuning& tuning = ParticleRenderTuning::Embedded());
    const std::vector<ParticleGpuInstance>& Instances() const { return instances_; }

private:
    struct Entry {
        std::size_t index;
        double depth;
        double radius;
        double alpha;
    };
    std::vector<Entry> sorted_;
    std::vector<ParticleGpuInstance> instances_;
};

} // namespace rs::render

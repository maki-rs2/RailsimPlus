#include "ParticleBatchBuilder.h"

#include <algorithm>
#include <cmath>

#include "ParticlePass.h"

namespace rs::render {
namespace {

double LinearFromSrgb(double value) {
    // sRGBの伝達関数。PIのRGBはsRGBのままプールへ保持される。
    return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
}

ParticleGpuInstance Pack(const rs::sim::ParticleState& particle, const rs::core::DVec3& cameraPosition,
                          const rs::core::DVec3& cameraForward, double depth, double radius, double alpha,
                          const ParticleRenderTuning& tuning) {
    const rs::core::DVec3 relative = rs::core::Sub(particle.position, cameraPosition);
    const double width = 2.0 * radius;
    double length = width;
    rs::core::DVec3 stretchDirection = {};
    if (particle.blendMode == rs::legacy_pi::ParticleBlendMode::Add) {
        const rs::core::DVec3 projected = rs::core::Sub(particle.velocity,
            rs::core::Scale(cameraForward, rs::core::Dot(particle.velocity, cameraForward)));
        const double speed = rs::core::Length(projected);
        // m/sの射影速度がほぼゼロなら、正規化せず正対の板にする。
        constexpr double kMinStretchSpeedMetersPerSecond = 1e-6;
        if (speed > kMinStretchSpeedMetersPerSecond) {
            stretchDirection = rs::core::Scale(projected, 1.0 / speed);
            length = std::min(width + speed * tuning.stretchSeconds, width * tuning.maxStretchRatio);
        }
    }
    ParticleGpuInstance instance;
    instance.position[0] = static_cast<float>(relative.x);
    instance.position[1] = static_cast<float>(relative.y);
    instance.position[2] = static_cast<float>(relative.z);
    instance.radius = static_cast<float>(radius);
    for (std::size_t channel = 0; channel < 3; ++channel) {
        instance.color[channel] = static_cast<float>(LinearFromSrgb(particle.color[channel]));
    }
    instance.color[3] = static_cast<float>(alpha);
    instance.angleRadians = static_cast<float>(particle.angleRadians);
    instance.textureSlot = particle.textureSlot;
    instance.blendMode = static_cast<std::uint32_t>(particle.blendMode);
    instance.velocity[0] = static_cast<float>(particle.velocity.x);
    instance.velocity[1] = static_cast<float>(particle.velocity.y);
    instance.velocity[2] = static_cast<float>(particle.velocity.z);
    instance.depth = static_cast<float>(depth);
    instance.stretchDirection[0] = static_cast<float>(stretchDirection.x);
    instance.stretchDirection[1] = static_cast<float>(stretchDirection.y);
    instance.stretchDirection[2] = static_cast<float>(stretchDirection.z);
    instance.length = static_cast<float>(length);
    return instance;
}

} // namespace

std::size_t ParticleBatchBuilder::Build(const rs::sim::ParticlePool& pool,
                                        const rs::core::DVec3& cameraPosition,
                                        const rs::core::DVec3& cameraForward,
                                        const std::function<bool(std::uint32_t)>& hasTexture,
                                        std::size_t maxParticles, const ParticleRenderTuning& tuning) {
    sorted_.clear();
    instances_.clear();
    // 呼び出し側は注視点との差を渡すため、深さをmで扱えるよう正規化する。
    const rs::core::DVec3 forward = rs::core::Normalize(cameraForward);
    for (std::size_t index = 0; index < pool.Size(); ++index) {
        const rs::sim::ParticleState particle = pool.Get(index);
        const double depth = rs::core::Dot(rs::core::Sub(particle.position, cameraPosition), forward);
        const double nearFade = std::clamp((depth - tuning.nearFadeStartMeters) /
            (tuning.nearFadeEndMeters - tuning.nearFadeStartMeters), 0.0, 1.0);
        const double alpha = particle.color[3] * particle.Alpha() * nearFade;
        const double radius = particle.RadiusMeters();
        if (particle.textureSlot >= kMaxParticleTextures || !hasTexture(particle.textureSlot) ||
            particle.ageSeconds > particle.lifetimeSeconds || !(alpha > 0.0) || !(radius > 0.0)) continue;
        sorted_.push_back({index, depth, radius, alpha});
    }
    std::stable_sort(sorted_.begin(), sorted_.end(), [](const Entry& left, const Entry& right) {
        return left.depth > right.depth;
    });
    const std::size_t count = std::min(sorted_.size(), maxParticles);
    const std::size_t dropped = sorted_.size() - count;
    instances_.reserve(count);
    for (std::size_t index = dropped; index < sorted_.size(); ++index) {
        const Entry& entry = sorted_[index];
        instances_.push_back(Pack(pool.Get(entry.index), cameraPosition, forward,
                                  entry.depth, entry.radius, entry.alpha, tuning));
    }
    return dropped;
}

} // namespace rs::render

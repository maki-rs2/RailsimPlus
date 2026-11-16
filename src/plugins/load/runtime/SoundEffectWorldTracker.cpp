// SoundEffectWorldTracker.cpp - SoundEffect のワールド位置と速度の追跡。
#include "SoundEffectWorldTracker.h"

#include "../../../core/SpeedUnits.h"

namespace rs::app {

void UpdateSoundEffectWorldPositions(
    const std::vector<SoundEffectAttachment>& attachments,
    const std::vector<rs::core::Mat4>& dynamicWorlds, const rs::core::Mat4& entityWorld,
    float dt, bool continuousMotion, std::vector<SoundEffectRuntimeState>& states) {
    const size_t updateCount = (attachments.size() < states.size()) ? attachments.size() : states.size();
    for (size_t i = 0; i < updateCount; ++i) {
        const SoundEffectAttachment& attachment = attachments[i];
        SoundEffectRuntimeState& state = states[i];
        if (attachment.partDefIndex < 0 ||
            static_cast<size_t>(attachment.partDefIndex) >= dynamicWorlds.size()) {
            state.velocity = rs::core::Vec3{};
            continue;
        }

        const rs::core::Mat4 localTransform = rs::core::Mat4::Mul(
            rs::core::MakeTranslation(attachment.sourceCoord.x, attachment.sourceCoord.y,
                                      attachment.sourceCoord.z),
            dynamicWorlds[static_cast<size_t>(attachment.partDefIndex)]);
        const rs::core::Mat4 worldTransform = rs::core::Mat4::Mul(localTransform, entityWorld);
        const rs::core::Vec3 worldPos{worldTransform.m[12], worldTransform.m[13],
                                      worldTransform.m[14]};

        // 初回は前回位置が原点のままなので、差分を速度にすると大きなドップラーがかかる。
        if (continuousMotion && dt > 0.0f && state.hasWorldPos) {
            state.velocity = rs::core::Vec3{(worldPos.x - state.worldPos.x) / dt,
                                            (worldPos.y - state.worldPos.y) / dt,
                                            (worldPos.z - state.worldPos.z) / dt};
        } else {
            state.velocity = rs::core::Vec3{};
        }
        state.worldPos = worldPos;
        state.hasWorldPos = true;
    }
}

rs::core::Vec3 ComputeTrainSoundVelocity(const rs::core::Vec3& tangent, double signedSpeedKmh) {
    const double speedMetersPerSecond = rs::core::KilometersPerHourToMetersPerSecond(signedSpeedKmh);
    return rs::core::Vec3{static_cast<float>(tangent.x * speedMetersPerSecond),
                          static_cast<float>(tangent.y * speedMetersPerSecond),
                          static_cast<float>(tangent.z * speedMetersPerSecond)};
}

void SetSoundEffectVelocities(std::vector<SoundEffectRuntimeState>& states,
                              const rs::core::Vec3& velocity) {
    for (SoundEffectRuntimeState& state : states) state.velocity = velocity;
}

void ZeroSoundEffectVelocities(std::vector<SoundEffectRuntimeState>& states) {
    for (SoundEffectRuntimeState& state : states) state.velocity = rs::core::Vec3{};
}

} // namespace rs::app

// SoundEffectWorldTracker.h - SoundEffect のワールド位置と速度の追跡。
#pragma once

#include <vector>

#include "SoundEffectAttachment.h"

namespace rs::app {

struct SoundEffectRuntimeState {
    rs::core::Vec3 worldPos{0, 0, 0};
    rs::core::Vec3 velocity{0, 0, 0};
    // UpdateSoundEffectWorldPositionsで一度でも位置を求めたか。falseの間のworldPosは
    // 初期値の原点のままなので、音源として鳴らさず、速度の差分の起点にも使わない。
    bool hasWorldPos = false;
};

void UpdateSoundEffectWorldPositions(
    const std::vector<SoundEffectAttachment>& attachments,
    const std::vector<rs::core::Mat4>& dynamicWorlds, const rs::core::Mat4& entityWorld,
    float dt, bool continuousMotion, std::vector<SoundEffectRuntimeState>& states);

// 走行速度と軌道接線から音源速度を求める。signedSpeedKmhの符号はtangent方向を正とする
// (車両の連結向きreversedは物理速度に無関係なので入れない)。位置差分ではなく解析的に
// 求めるのは、位置の更新間隔と差分に使う時間の刻みが揃わず値が振動するため。
rs::core::Vec3 ComputeTrainSoundVelocity(const rs::core::Vec3& tangent, double signedSpeedKmh);

// 編成のように音源が剛体としてまとまって動く場合に、求めた速度を一括で書き込む。
void SetSoundEffectVelocities(std::vector<SoundEffectRuntimeState>& states,
                              const rs::core::Vec3& velocity);

void ZeroSoundEffectVelocities(std::vector<SoundEffectRuntimeState>& states);

} // namespace rs::app

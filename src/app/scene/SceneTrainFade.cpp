#include "../../render/RenderPrecision.h"
#include "SceneTrainFade.h"

#include "../../plugins/load/TrainAssembly.h"

#include <algorithm>
#include <cmath>

namespace rs::app {

void UpdateTrainWarpFade(SceneSlotPool::Slot& slot, float fadeOutAlpha,
                           bool spansScenes, double dsMeters, bool groupMoving) {
    if (spansScenes) {
        slot.carSnapshot.carFadeAlpha = 0.0f;
        slot.carSnapshot.warpFadeDistanceM = 0.0;
        slot.carSnapshot.warpFadeInActive = true;
        return;
    }
    if (slot.carSnapshot.warpFadeInActive) {
        slot.carSnapshot.warpFadeDistanceM += std::abs(dsMeters);
        slot.carSnapshot.carFadeAlpha = rs::render::ToGpuFloat(std::min(
            slot.carSnapshot.warpFadeDistanceM / rs::legacy_plugin::kWarpFadeLengthM, 1.0));
        if (slot.carSnapshot.carFadeAlpha >= 1.0f) slot.carSnapshot.warpFadeInActive = false;
        return;
    }
    // fadeOutAlphaは「先読みN[m]以内にwarpがあるか」という静止した幾何学的距離だけで
    // 決まる値のため、停車位置がたまたまwarp地点の近くにあるだけの車両にまで適用すると、
    // 停車中ずっと中途半端な透明度に固定されたまま変化しなくなる。
    // 実際にwarpへ向かって進んでいる間だけ滑らかにフェードアウトさせ、停車中は等倍で描く。
    slot.carSnapshot.carFadeAlpha = groupMoving ? std::clamp(fadeOutAlpha, 0.0f, 1.0f) : 1.0f;
}

void ResetTrainWarpFade(SceneSlotPool::Slot& slot) {
    slot.carSnapshot.carFadeAlpha = 1.0f;
    slot.carSnapshot.warpFadeDistanceM = 0.0;
    slot.carSnapshot.warpFadeInActive = false;
}

} // namespace rs::app

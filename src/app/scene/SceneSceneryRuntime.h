#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>

#include "SceneSlotPool.h"
#include "../../sim/PlatformApproach.h"
#include "../../world/scene/SceneRegistry.h"
#include "../../world/time/GameClock.h"
#include "../../world/time/SolarPosition.h"

namespace rs::app {

class App;

class SceneSceneryRuntime {
public:
    void Tick(App& app, rs::world::SceneRegistry& registry,
              rs::world::TrackGraph* activeGraph, std::vector<SceneSlotPool::Slot>& slots,
              uint64_t activeGraphRevision, double dt,
              const std::unordered_map<rs::world::Guid, rs::sim::StationApproachMask,
                                       rs::world::GuidHash>& approachMasks,
              const rs::world::GameClock& clock, const rs::world::SolarState& sun,
              double latitudeDeg, double nightThresholdSin, const float eye[3],
              float viewerFovYRadians);
    void UpdateAnimations(App& app, std::vector<SceneSlotPool::Slot>& slots);
    size_t LastTickRecomputedSlots() const { return lastTickRecomputedSlots_; }

    // 一時的な配置物(isTemporary)のスロットで、Tickが日付・時刻・_SEASONを書くか。既定は書く。
    // プラグインビューアーは、これらをビューアー設定の値のまま保つため偽にする
    // (昼夜係数・_NIGHT・_SHADOW・_ENVMAP・_CAMDISTは偽でも毎回更新する)。
    void SetWriteCalendarForTemporary(bool write) { writeCalendarForTemporary_ = write; }

private:
    bool writeCalendarForTemporary_ = true;
    size_t lastTickRecomputedSlots_ = 0;
    size_t lastAnimatedSlotCount_ = SIZE_MAX;
};

} // namespace rs::app

//
// ShadowDistance.cpp - シャドウ距離の選択肢から、距離[m]を持つ RenderTuning のメンバを引く
//
#include "ShadowDistance.h"

#include <array>
#include <cstddef>

#include "../resource/RenderTuning.h"

namespace rs::render {

namespace {

// kShadowDistanceChoices と同じ並び。選択肢ごとの距離[m]を持つ RenderTuning のメンバ。
constexpr std::array<float RenderTuning::*, static_cast<std::size_t>(ShadowDistance::Count)> kShadowDistanceMeters = {
    &RenderTuning::shadowDistanceNear,
    &RenderTuning::shadowDistanceStandard,
    &RenderTuning::shadowDistanceFar,
};

} // namespace

float ShadowDistanceMeters(const RenderTuning& tuning, ShadowDistance distance) {
    return tuning.*(kShadowDistanceMeters[static_cast<std::size_t>(distance)]);
}

} // namespace rs::render

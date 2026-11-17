//
// ShadowDistance.h - シャドウ距離の選択肢と、選択肢ごとの違いを持つ表
//
// 選ぶ値はユーザー設定(settings.dat の shadowDistance)。実際の距離[m]は開発調整値(RenderTuning)に置く。
//
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace rs::render {

struct RenderTuning;

enum class ShadowDistance : std::uint8_t {
    Near,
    Standard,
    Far,
    Count,
};

// 画面の選択肢の並びはこの表と同じ。keyは保存する文字列。距離[m]を持つRenderTuningのメンバは ShadowDistance.cpp に置く
// (RenderTuning が変わるたびに、この表を読む .cpp が再コンパイルにならないようにするため)。
struct ShadowDistanceChoice {
    ShadowDistance distance;
    const char* key;
};

constexpr std::array<ShadowDistanceChoice, static_cast<std::size_t>(ShadowDistance::Count)> kShadowDistanceChoices = {{
    {ShadowDistance::Near, "near"},
    {ShadowDistance::Standard, "standard"},
    {ShadowDistance::Far, "far"},
}};

constexpr ShadowDistance kDefaultShadowDistance = ShadowDistance::Standard;

// 保存した文字列を選択肢へ変える。読めない文字列は既定。
constexpr ShadowDistance ShadowDistanceFromKey(std::string_view key) {
    for (const ShadowDistanceChoice& choice : kShadowDistanceChoices) {
        if (key == choice.key) return choice.distance;
    }
    return kDefaultShadowDistance;
}

constexpr const char* ShadowDistanceKey(ShadowDistance distance) {
    return kShadowDistanceChoices[static_cast<std::size_t>(distance)].key;
}

// 選択肢の距離[m]。
float ShadowDistanceMeters(const RenderTuning& tuning, ShadowDistance distance);

} // namespace rs::render

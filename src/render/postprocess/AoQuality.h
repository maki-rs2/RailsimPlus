//
// AoQuality.h - アンビエントオクルージョン(GTAO)の品質プリセットと、プリセットごとの違いを持つ表
//
// 種類ごとの違いは分岐ではなく、この表に持つ。選ぶ値はユーザー設定(settings.dat の ambientOcclusion)。
//
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace rs::render {

enum class AoQuality : std::uint8_t {
    None,   // AO のパス全体(プリパス含む)を飛ばす
    Low,    // 半解像度、1方向
    Medium, // 全解像度、2方向
    High,   // 全解像度、3方向・刻み多め
    Count,
};

struct AoPresetSpec {
    bool halfResolution;       // AO を画面の半分の大きさで求めるか
    std::uint32_t sliceCount;  // 1画素あたりの方向(スライス)の数
    std::uint32_t stepCount;   // 1方向・片側あたりの刻みの数
};

constexpr std::array<AoPresetSpec, static_cast<std::size_t>(AoQuality::Count)> kAoPresets = {{
    {false, 0, 0},
    {true, 1, 3},
    {false, 2, 3},
    {false, 3, 4},
}};

constexpr AoPresetSpec AoSpecOf(AoQuality quality) {
    return kAoPresets[static_cast<std::size_t>(quality)];
}

// ユーザー設定(settings.dat)で選べる品質と、保存する文字列。画面の選択肢はこの表の並びと同じ。
struct AoQualityChoice {
    AoQuality quality;
    const char* key;
};

constexpr std::array<AoQualityChoice, 4> kAoQualityChoices = {{
    {AoQuality::None, "none"},
    {AoQuality::Low, "low"},
    {AoQuality::Medium, "medium"},
    {AoQuality::High, "high"},
}};

constexpr AoQuality kDefaultAoQuality = AoQuality::Medium;

// 保存した文字列を品質へ変える。読めない文字列は既定。
constexpr AoQuality AoQualityFromKey(std::string_view key) {
    for (const AoQualityChoice& choice : kAoQualityChoices) {
        if (key == choice.key) return choice.quality;
    }
    return kDefaultAoQuality;
}

// 品質を保存する文字列へ変える。
constexpr const char* AoQualityKey(AoQuality quality) {
    const char* key = kAoQualityChoices.front().key;
    for (const AoQualityChoice& choice : kAoQualityChoices) {
        if (choice.quality == quality) key = choice.key;
    }
    return key;
}

} // namespace rs::render

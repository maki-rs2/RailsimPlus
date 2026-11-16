//
// ShadowQuality.h - シャドウの品質プリセットと、プリセットごとの違いを持つ表
//
// 種類ごとの違いは分岐ではなく、この表に持つ。選ぶ値はユーザー設定(settings.dat の shadowQuality)。
//
#pragma once

#include "../../core/Epsilon.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace rs::render {

enum class ShadowQuality : std::uint8_t {
    None,   // シャドウのパス全体を飛ばす
    Low,    // 1枚
    Medium, // 2枚
    High,   // 4枚・境界ブレンド
    Count,
};

// ピクセルシェーダが使うフィルタの種類(b1 の shadowParams.w の値)。
enum class ShadowFilter : std::uint32_t {
    HardwarePcf1Tap = 0, // 比較サンプラ(2x2のハードウェアPCF)を1タップ
    PoissonBlur = 1,     // 回転Poissonディスクの固定幅ぼかし
    Pcss = 2,            // 遮蔽物探索でぼけ幅を決めるぼかし
};

struct ShadowPresetSpec {
    std::uint32_t cascadeCount;
    std::uint32_t mapSize; // 1枚の一辺[テクセル]
    ShadowFilter filter;
    bool cascadeBlend; // カスケードの境界で隣の枚とブレンドするか
    bool thinCasters; // 細い部品(SubMesh::thinShadow)も影パスで描くか
    bool narrowBlur; // ぼけ幅を細い方の値にするか(細い部品の影が消えないように)
    std::uint32_t blurTapCount; // ぼかしのタップ数(シェーダの表は8点と16点。0なら1タップ)
};

constexpr std::array<ShadowPresetSpec, static_cast<std::size_t>(ShadowQuality::Count)> kShadowPresets = {{
    {0, 0, ShadowFilter::HardwarePcf1Tap, false, false, false, 0},
    {1, 2048, ShadowFilter::PoissonBlur, false, false, false, 8},
    {2, 2048, ShadowFilter::PoissonBlur, false, false, false, 16},
    {4, 4096, ShadowFilter::Pcss, true, true, true, 16},
}};

// ぼけ幅(半径)[m]をシャドウマップのテクセル数へ換える。シェーダの SampleShadowBlur と同じ式。
constexpr float ShadowBlurRadiusTexels(float blurRadiusM, float texelWorldSizeM) {
    return blurRadiusM / (texelWorldSizeM > rs::core::kShadowTexelSizeEpsM ? texelWorldSizeM : rs::core::kShadowTexelSizeEpsM);
}

constexpr ShadowPresetSpec ShadowSpecOf(ShadowQuality quality) {
    return kShadowPresets[static_cast<std::size_t>(quality)];
}

// ユーザー設定(settings.dat)で選べる品質と、保存する文字列。画面の選択肢はこの表の並びと同じ。
struct ShadowQualityChoice {
    ShadowQuality quality;
    const char* key;
};

constexpr std::array<ShadowQualityChoice, 4> kShadowQualityChoices = {{
    {ShadowQuality::None, "none"},
    {ShadowQuality::Low, "low"},
    {ShadowQuality::Medium, "medium"},
    {ShadowQuality::High, "high"},
}};

constexpr ShadowQuality kDefaultShadowQuality = ShadowQuality::Low;

// 保存した文字列を品質へ変える。読めない文字列は既定。
constexpr ShadowQuality ShadowQualityFromKey(std::string_view key) {
    for (const ShadowQualityChoice& choice : kShadowQualityChoices) {
        if (key == choice.key) return choice.quality;
    }
    return kDefaultShadowQuality;
}

// 品質を保存する文字列へ変える。
constexpr const char* ShadowQualityKey(ShadowQuality quality) {
    const char* key = kShadowQualityChoices.front().key;
    for (const ShadowQualityChoice& choice : kShadowQualityChoices) {
        if (choice.quality == quality) key = choice.key;
    }
    return key;
}

} // namespace rs::render

//
// LandscapeContrast.h - 標準の空の遠景の明暗の幅(landscapeContrast)を広げるための、画像の統計(純関数)
//
// LandscapeShader.h の ApplyLandscapeContrast と同じ式(変えるときは両方)。
// アルベドの輝度 L を、中心 pivot(画像の輝度の幾何平均)から対数の上で contrast 倍に広げ、色の比を保ったまま
// meanScale を掛け、最大のチャンネルを1で止める。meanScale は、広げたあとの輝度の平均(カバレッジで重み付け)が
// 元の画像の平均と同じになる値(明暗の差だけを強め、平均の明るさは変えない)。
//
#pragma once

#include <cstdint>

namespace rs::render {

struct LandscapeContrastStats {
    float pivotLuminance = 0.18f; // 輝度の幾何平均(リニア)
    float meanScale = 1.0f;       // 平均の明るさを保つ倍率
};

// rgba: sRGBの8bit RGBA(width×height、行の詰め物なし)。アルファ0の画素は数えない。
// 不透明な画素がないときは既定値を返す。
LandscapeContrastStats ComputeLandscapeContrastStats(const std::uint8_t* rgba, std::uint32_t width,
                                                     std::uint32_t height, float contrast);

// 1画素(リニアのアルベド)に明暗の幅を掛けた結果。シェーダの ApplyLandscapeContrast と同じ式。
void ApplyLandscapeContrast(const float albedo[3], float contrast, const LandscapeContrastStats& stats,
                            float out[3]);

} // namespace rs::render

//
// MotionBlurMath.h - モーションブラーのCPU側の数値(GPUに依存しない純関数)
//
// ぼかしの半径の上限と、速度の最大値を求めるタイルの大きさ・個数。
// 速度の向き・長さの計算とぼかしの重みはシェーダ(MotionBlurShader.h)にだけ置く。
//
#pragma once

#include <algorithm>
#include <cstdint>

namespace rs::render {

// 速度の最大値を求めるタイルの1辺[画素]。ぼかしの半径の上限(MotionBlurMaxRadiusPixels)以上であること。
// 周囲 3x3 のタイルの最大の速度を見れば、その画素を通る全部のぼかしの向きが分かる。
constexpr std::uint32_t kMotionBlurTileSize = 64;

// 画面の大きさ(画素)を覆うタイルの数(端の欠けたタイルも1つに数える)。
constexpr std::uint32_t MotionBlurTileCount(std::uint32_t pixels) {
    return (pixels + kMotionBlurTileSize - 1) / kMotionBlurTileSize;
}

// ぼかしの半径(中心から前後へ広げる片側の長さ)の上限[画素]。画面の高さに対する割合から求め、
// 1画素より小さくせず、タイル1枚の大きさで頭打ちにする(それ以上は周囲のタイルの範囲を超える)。
constexpr float MotionBlurMaxRadiusPixels(std::uint32_t screenHeight, float heightFraction) {
    return std::clamp(heightFraction * static_cast<float>(screenHeight), 1.0f, static_cast<float>(kMotionBlurTileSize));
}

// ぼかしの長さの基準にするフレーム時間[秒]。速度はフレーム間の動きなので、FPSが低いほど動きもぼかしも伸びる。
// これより長いフレームでは、シャッターの割合を基準との比で縮めて、ぼかしの長さを「基準時間ぶんの動き」に揃える。
constexpr float kMotionBlurReferenceFrameSeconds = 1.0f / 60.0f;

// フレーム時間に合わせたシャッターの割合。基準より短いフレーム(高FPS)では変えない。
// 長すぎるフレームで0に近づきすぎないよう、縮める下限は基準の1/8とする。
constexpr float MotionBlurShutterForFrameTime(float shutter, float frameSeconds) {
    if (frameSeconds <= kMotionBlurReferenceFrameSeconds) return shutter;
    return shutter * std::max(kMotionBlurReferenceFrameSeconds / frameSeconds, 0.125f);
}

// 撮影カメラのシャッター速度[秒]から求めるシャッターの割合。ぼかしの長さを「シャッターが開いている間の動き」にするため、
// 1フレームの動きに対する比(シャッター速度 / フレーム時間)にする。フレーム時間が不正なときは基準のフレーム時間で割る。
constexpr float MotionBlurShutterForExposure(float shutterSeconds, float frameSeconds) {
    if (!(shutterSeconds > 0.0f)) return 0.0f;
    const float frame = frameSeconds > 0.0f ? frameSeconds : kMotionBlurReferenceFrameSeconds;
    return shutterSeconds / frame;
}

// 速度の向きに拾う点の数。極端な値を許さない(速度の向きの刻みが粗すぎる・重すぎるのを防ぐ)。
constexpr std::uint32_t kMotionBlurMinSamples = 4;
constexpr std::uint32_t kMotionBlurMaxSamples = 32;
constexpr std::uint32_t ClampMotionBlurSamples(int samples) {
    return static_cast<std::uint32_t>(
        std::clamp(samples, static_cast<int>(kMotionBlurMinSamples), static_cast<int>(kMotionBlurMaxSamples)));
}

} // namespace rs::render

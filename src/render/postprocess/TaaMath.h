//
// TaaMath.h - TAA のジッタ(サブピクセルのずれ)の計算。GPU に依存しない純関数。
//
// 主視点の射影へ足すずれは、Halton(2,3) の列から取る。ずれは NDC で表し、クリップ座標の x・y に
// `ずれ * w` を足す(row-vector 慣習の viewProj では、第0・1列に第3列の定数倍を足す)。
//
#pragma once

#include <cstdint>

namespace rs::render {

// ジッタの列の長さ。
constexpr std::uint32_t kTaaJitterSampleCount = 8;

// Halton 列の index 番目(index >= 1)。基数 base。0〜1。
constexpr float HaltonSequence(std::uint32_t index, std::uint32_t base) {
    float result = 0.0f;
    float fraction = 1.0f / static_cast<float>(base);
    while (index > 0) {
        result += static_cast<float>(index % base) * fraction;
        index /= base;
        fraction /= static_cast<float>(base);
    }
    return result;
}

// frameIndex フレーム目のずれ[画素]。x・y とも -0.5〜0.5(画素の中心からの距離)。
struct TaaJitterPixels {
    float x = 0.0f;
    float y = 0.0f;
};
constexpr TaaJitterPixels TaaJitterForFrame(std::uint32_t frameIndex) {
    const std::uint32_t index = frameIndex % kTaaJitterSampleCount + 1;
    return {HaltonSequence(index, 2) - 0.5f, HaltonSequence(index, 3) - 0.5f};
}

// 画素のずれ[画素。右・下が正]を NDC のずれ(右・上が正)へ変える。
struct TaaJitterNdc {
    float x = 0.0f;
    float y = 0.0f;
};
constexpr TaaJitterNdc TaaJitterToNdc(TaaJitterPixels pixels, std::uint32_t width, std::uint32_t height) {
    return {2.0f * pixels.x / static_cast<float>(width), -2.0f * pixels.y / static_cast<float>(height)};
}

// row-vector 慣習の viewProj(row-major の 16 要素)へ、NDC のずれを足す。
// 画面に映る位置が NDC で (jitter.x, jitter.y) だけずれる(透視除算のあとも同じ量)。
constexpr void ApplyNdcJitter(float viewProj[16], TaaJitterNdc jitter) {
    for (int row = 0; row < 4; ++row) {
        viewProj[row * 4 + 0] += jitter.x * viewProj[row * 4 + 3];
        viewProj[row * 4 + 1] += jitter.y * viewProj[row * 4 + 3];
    }
}

} // namespace rs::render

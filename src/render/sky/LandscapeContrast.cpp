#include "../RenderPrecision.h"
#include "LandscapeContrast.h"
#include "../pipeline/ShaderColor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "../postprocess/TonemapMath.h"

namespace rs::render {

namespace {

// 対数を取るときの輝度の下限(真っ黒の画素で log が発散しないように)。シェーダの kLandscapeLuminanceMin と同じ。
constexpr float kLuminanceMin = 1e-4f;
// 統計を取る画素の間隔(縦横。画像は数百万画素あるので、間引いても平均はほぼ変わらない)。
constexpr std::uint32_t kSampleStride = 4;
// 倍率の二分探索の範囲と回数(倍率は1前後。広げると明るい側が伸びて平均が上がるので、ふつうは1より小さい)。
constexpr float kScaleSearchMax = 4.0f;
constexpr int kScaleSearchIterations = 24;

float Luminance(const float rgb[3]) {
    return rgb[0] * kRec709LuminanceWeights<float>[0] + rgb[1] * kRec709LuminanceWeights<float>[1] + rgb[2] * kRec709LuminanceWeights<float>[2];
}

struct Sample {
    std::array<float, 3> rgb;
    float weight;
};

// 広げたあと(倍率 scale)の、カバレッジで重み付けした輝度の平均。
float MeanAfter(const std::vector<Sample>& samples, float contrast, LandscapeContrastStats stats, float scale) {
    stats.meanScale = scale;
    double sum = 0.0;
    double weightSum = 0.0;
    for (const Sample& s : samples) {
        float out[3] = {};
        ApplyLandscapeContrast(s.rgb.data(), contrast, stats, out);
        sum += static_cast<double>(Luminance(out)) * s.weight;
        weightSum += s.weight;
    }
    return weightSum > 0.0 ? rs::render::ToGpuFloat(sum / weightSum) : 0.0f;
}

} // namespace

void ApplyLandscapeContrast(const float albedo[3], float contrast, const LandscapeContrastStats& stats,
                            float out[3]) {
    const float luminance = std::max(Luminance(albedo), kLuminanceMin);
    const float pivot = std::max(stats.pivotLuminance, kLuminanceMin);
    const float contrasted = pivot * std::pow(luminance / pivot, contrast);
    const float factor = contrasted / luminance * stats.meanScale;
    float peak = 0.0f;
    for (int i = 0; i < 3; ++i) {
        out[i] = albedo[i] * factor;
        peak = std::max(peak, out[i]);
    }
    if (peak > 1.0f) {
        for (int i = 0; i < 3; ++i) out[i] /= peak;
    }
}

LandscapeContrastStats ComputeLandscapeContrastStats(const std::uint8_t* rgba, std::uint32_t width,
                                                     std::uint32_t height, float contrast) {
    LandscapeContrastStats stats;
    if (rgba == nullptr || width == 0 || height == 0) return stats;

    std::array<float, 256> toLinear = {};
    for (int i = 0; i < 256; ++i) toLinear[i] = SrgbToLinear(static_cast<float>(i) / 255.0f);

    std::vector<Sample> samples;
    samples.reserve(static_cast<std::size_t>(width / kSampleStride + 1) * (height / kSampleStride + 1));
    double logSum = 0.0;
    double meanSum = 0.0;
    double weightSum = 0.0;
    for (std::uint32_t y = 0; y < height; y += kSampleStride) {
        for (std::uint32_t x = 0; x < width; x += kSampleStride) {
            const std::uint8_t* p = rgba + (static_cast<std::size_t>(y) * width + x) * 4;
            if (p[3] == 0) continue;
            Sample s = {{toLinear[p[0]], toLinear[p[1]], toLinear[p[2]]}, static_cast<float>(p[3]) / 255.0f};
            const float luminance = Luminance(s.rgb.data());
            logSum += std::log2(std::max(luminance, kLuminanceMin)) * s.weight;
            meanSum += static_cast<double>(luminance) * s.weight;
            weightSum += s.weight;
            samples.push_back(s);
        }
    }
    if (weightSum <= 0.0) return stats;
    stats.pivotLuminance = rs::render::ToGpuFloat(std::exp2(logSum / weightSum));
    const float targetMean = rs::render::ToGpuFloat(meanSum / weightSum);

    // 平均は倍率に対して単調に増える(1で止める分も含めて)ので、二分探索で合わせる。
    float low = 0.0f;
    float high = kScaleSearchMax;
    for (int i = 0; i < kScaleSearchIterations; ++i) {
        const float mid = 0.5f * (low + high);
        if (MeanAfter(samples, contrast, stats, mid) < targetMean) {
            low = mid;
        } else {
            high = mid;
        }
    }
    stats.meanScale = 0.5f * (low + high);
    return stats;
}

} // namespace rs::render

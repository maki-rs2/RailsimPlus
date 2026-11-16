#include "EnvPhotoStats.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <numbers>

#include "../postprocess/TonemapMath.h"
#include "AtmosphereMath.h"
#include "../pipeline/ShaderColor.h"

namespace rs::render {

namespace {

struct Texel {
    double dir[3];     // 正規化済みの方向
    double rgb[3];     // リニア
    double weight;     // コサイン重み = テクセルの立体角 × 方向のy
};

float HalfToFloat(std::uint16_t h) {
    const std::uint32_t sign = static_cast<std::uint32_t>(h & 0x8000u) << 16;
    const std::uint32_t exponent = (h >> 10) & 0x1Fu;
    std::uint32_t mantissa = h & 0x3FFu;
    std::uint32_t bits;
    if (exponent == 0) {
        if (mantissa == 0) {
            bits = sign;
        } else {  // 非正規化数: 正規化して表す
            std::uint32_t e = 127 - 15 + 1;
            while ((mantissa & 0x400u) == 0) {
                mantissa <<= 1;
                --e;
            }
            mantissa &= 0x3FFu;
            bits = sign | (e << 23) | (mantissa << 13);
        }
    } else if (exponent == 31) {
        bits = sign | 0x7F800000u | (mantissa << 13);
    } else {
        bits = sign | ((exponent + 127 - 15) << 23) | (mantissa << 13);
    }
    return std::bit_cast<float>(bits);
}

std::size_t BytesPerTexel(EnvTexelFormat format) {
    switch (format) {
    case EnvTexelFormat::Rgba8Srgb: return 4;
    case EnvTexelFormat::Rgba16Float: return 8;
    case EnvTexelFormat::Rgba32Float: return 16;
    }
    return 0;
}

void ReadRgb(EnvTexelFormat format, const std::uint8_t* p, double rgb[3]) {
    for (size_t c = 0; c < 3; ++c) {
        switch (format) {
        case EnvTexelFormat::Rgba8Srgb: rgb[c] = SrgbToLinear(static_cast<float>(p[c]) / 255.0f); break;
        case EnvTexelFormat::Rgba16Float: {
            std::uint16_t h;
            std::memcpy(&h, p + c * 2, sizeof(h));
            rgb[c] = HalfToFloat(h);
            break;
        }
        case EnvTexelFormat::Rgba32Float: {
            float f;
            std::memcpy(&f, p + c * 4, sizeof(f));
            rgb[c] = f;
            break;
        }
        }
        if (!std::isfinite(rgb[c]) || rgb[c] < 0.0) rgb[c] = 0.0;
    }
}

double Luma(const double rgb[3]) { return kRec709LuminanceWeights<double>[0] * rgb[0] + kRec709LuminanceWeights<double>[1] * rgb[1] +
           kRec709LuminanceWeights<double>[2] * rgb[2]; }

// 上半球(y>0)のテクセルを全部たどる。
template <typename Fn>
void ForEachUpperTexel(const EnvTextureData& cube, Fn&& fn) {
    const std::size_t stride = BytesPerTexel(cube.format);
    const double size = static_cast<double>(cube.width);
    for (std::uint32_t face = 0; face < 6; ++face) {
        const EnvTextureSubresource& sub = cube.subresources[static_cast<std::size_t>(face) * cube.mipLevels];
        for (std::uint32_t y = 0; y < cube.height; ++y) {
            const double ty = (static_cast<double>(y) + 0.5) / size;
            for (std::uint32_t x = 0; x < cube.width; ++x) {
                const double tx = (static_cast<double>(x) + 0.5) / size;
                const atmosphere::CubeDirection d = atmosphere::CubeFaceDirection(static_cast<int>(face), tx, ty);
                if (d.y <= 0.0) continue;
                const double u = 2.0 * tx - 1.0;
                const double v = 2.0 * ty - 1.0;
                Texel texel;
                texel.dir[0] = d.x;
                texel.dir[1] = d.y;
                texel.dir[2] = d.z;
                // 面上の画素の立体角は (1+u^2+v^2)^-1.5 に比例する。
                texel.weight = std::pow(1.0 + u * u + v * v, -1.5) * d.y;
                ReadRgb(cube.format, cube.bytes.data() + sub.offset + y * sub.rowPitch + x * stride, texel.rgb);
                fn(texel);
            }
        }
    }
}

bool CubeIsUsable(const EnvTextureData& cube) {
    if (!cube.ok || !cube.isCube || cube.arraySize != 6 || cube.width == 0 || cube.width != cube.height ||
        cube.mipLevels == 0 || cube.subresources.size() < static_cast<std::size_t>(6) * cube.mipLevels) {
        return false;
    }
    const EnvTextureSubresource& last = cube.subresources[static_cast<std::size_t>(5) * cube.mipLevels];
    return last.offset + last.slicePitch <= cube.bytes.size() &&
           last.rowPitch >= static_cast<std::size_t>(cube.width) * BytesPerTexel(cube.format);
}

} // namespace

EnvPhotoStats ComputeEnvPhotoStats(const EnvTextureData& specularCube) {
    EnvPhotoStats stats;
    if (!CubeIsUsable(specularCube)) return stats;

    // 1回目: 上半球の平均輝度と、最も明るいテクセル。
    double weightSum = 0.0;
    double lumaSum = 0.0;
    double maxLuma = 0.0;
    double maxDir[3] = {0.0, 1.0, 0.0};
    ForEachUpperTexel(specularCube, [&](const Texel& t) {
        const double luma = Luma(t.rgb);
        weightSum += t.weight;
        lumaSum += t.weight * luma;
        if (luma > maxLuma) {
            maxLuma = luma;
            std::copy(t.dir, t.dir + 3, maxDir);
        }
    });
    if (weightSum <= 0.0) return stats;
    const double meanLuma = lumaSum / weightSum;
    stats.sunFound = meanLuma > 0.0 && maxLuma >= kPhotoSunMinRatio * meanLuma;
    if (stats.sunFound) std::copy(maxDir, maxDir + 3, stats.sunDirection);

    // 2回目: 太陽の周りを除いた平均色。
    const double excludeCos = std::cos(kPhotoSunExcludeDeg * std::numbers::pi / 180.0);
    double rgbSum[3] = {0.0, 0.0, 0.0};
    double keptWeight = 0.0;
    ForEachUpperTexel(specularCube, [&](const Texel& t) {
        if (stats.sunFound) {
            const double cosAngle = t.dir[0] * maxDir[0] + t.dir[1] * maxDir[1] + t.dir[2] * maxDir[2];
            if (cosAngle >= excludeCos) return;
        }
        keptWeight += t.weight;
        for (int c = 0; c < 3; ++c) rgbSum[c] += t.weight * t.rgb[c];
    });
    if (keptWeight <= 0.0) return stats;
    for (int c = 0; c < 3; ++c) stats.skyMean[c] = rgbSum[c] / keptWeight;
    stats.ok = std::isfinite(stats.skyMean[0] + stats.skyMean[1] + stats.skyMean[2]) &&
               stats.skyMean[0] + stats.skyMean[1] + stats.skyMean[2] > 0.0;
    return stats;
}

} // namespace rs::render

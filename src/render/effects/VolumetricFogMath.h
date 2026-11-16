//
// VolumetricFogMath.h - ボリュメトリックフォグの格子の割り方と積分の、CPU側の基準実装(純関数)
//
// シェーダ(VolumetricFogShader.h)と同じ式。変えるときは両方(テストはこちらを検証する)。
// 奥行きはカメラ前方向の距離で、手前を細かく割る指数分割。積分はセルの中の媒質を一定とみなす解析解で、
// スライスの厚さが変わっても明るさが変わらない。
//
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "../postprocess/TaaMath.h"

namespace rs::render::volumetric_fog {

// 指数分割の手前側の細かさを決める長さ[m]。大きいほど奥のスライスが細かくなる。HLSL の kSliceNearScaleM と同じ値。
inline constexpr double kSliceNearScaleM = 4.0;

// 消散係数がこれより小さいときは、積分の式の分母が0に近づくので、媒質が無い極限(長さそのもの)を使う。
// HLSL の kSmallExtinction と同じ値。
inline constexpr double kSmallExtinction = 1.0e-6;

// 画面の大きさに対する格子の数(余りが出ても画面を覆う最小の数)。
constexpr std::uint32_t GridCellCount(std::uint32_t pixels, std::uint32_t cellPixels) {
    return (pixels + cellPixels - 1) / cellPixels;
}

// セルの中で標本を取る位置のずれの列の長さ。
// 基数5の列は25の倍数で一巡するので、短く切ると奥行きが手前へ偏る。24個なら奥行きの平均が0、横方向も0.03セル以下で済む。
inline constexpr std::uint32_t kCellJitterSampleCount = 24;

// frameIndex フレーム目のずれ[セル]。x・y・奥行きとも -0.5〜0.5。履歴で平均すると、セルの中を一様に標本化したのに近づく。
struct CellJitter {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};
constexpr CellJitter CellJitterForFrame(std::uint32_t frameIndex) {
    const std::uint32_t index = frameIndex % kCellJitterSampleCount + 1;
    return {HaltonSequence(index, 2) - 0.5f, HaltonSequence(index, 3) - 0.5f, HaltonSequence(index, 5) - 0.5f};
}

// 格子の奥行きの位置(0..1)からカメラ前方向の距離[m]。0で0、1で maxDistance。
inline double SliceDepth(double unit, double maxDistance) {
    return kSliceNearScaleM * (std::exp(std::log(maxDistance / kSliceNearScaleM + 1.0) * unit) - 1.0);
}

// SliceDepth の逆。範囲の外は 0..1 へ収める。
inline double SliceUnit(double depth, double maxDistance) {
    const double unit = std::log(std::max(depth, 0.0) / kSliceNearScaleM + 1.0) /
                        std::log(maxDistance / kSliceNearScaleM + 1.0);
    return std::clamp(unit, 0.0, 1.0);
}

// 一定の媒質の中を length 進む間に、始点へ届く散乱光の、散乱の強さに対する割合 (1 - exp(-σ·d)) / σ。
inline double ScatterIntegral(double extinction, double length) {
    if (extinction < kSmallExtinction) return length;
    return (1.0 - std::exp(-extinction * length)) / extinction;
}

// カメラからある距離までの積分の途中経過。
struct Accumulated {
    double scatter = 0.0;
    double transmittance = 1.0;
};

// 散乱の強さ scatter・消散係数 extinction が一定のセルを length 進めた結果。
inline Accumulated Advance(const Accumulated& from, double scatter, double extinction, double length) {
    Accumulated to;
    to.scatter = from.scatter + from.transmittance * scatter * ScatterIntegral(extinction, length);
    to.transmittance = from.transmittance * std::exp(-extinction * length);
    return to;
}

} // namespace rs::render::volumetric_fog

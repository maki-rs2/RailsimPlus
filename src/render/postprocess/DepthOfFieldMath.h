//
// DepthOfFieldMath.h - 被写界深度のCPU側の数値(GPUに依存しない純関数)
//
// 薄レンズの式から、錯乱円(ピントが合わない点がセンサー上に作るにじみの円)の大きさと、
// ピントが合って見える範囲(近限・遠限)を求める。焦点距離・錯乱円・フレームの大きさは mm、
// ピント距離・奥行き・近限・遠限は m で受け渡す。
//
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace rs::render {

namespace dof_detail {
constexpr double kMmPerM = 1000.0;
// 奥行きの下限[m]。0 以下の奥行きで割り算が壊れないようにする。
constexpr double kMinDepthM = 1e-6;
} // namespace dof_detail

// 最短撮影距離[m]。焦点距離の2倍より近いと像距離が発散するので、ここより近いピント距離は使わない。
inline double MinFocusDistanceM(double focalLengthMm) {
    return 2.0 * focalLengthMm / dof_detail::kMmPerM;
}

// ピント距離[m]を最短撮影距離以上に収める。以降の式の s - f が 0 以下になるのを防ぐ。
inline double ClampFocusDistanceM(double focalLengthMm, double focusDistanceM) {
    return std::max(focusDistanceM, MinFocusDistanceM(focalLengthMm));
}

// 奥行き depthM[m]の点がセンサー上に作る錯乱円の直径[mm]。c = f^2 / (N (s - f)) * |d - s| / d。
// 奥行きが無限大のときは |d - s| / d を 1 とみなす。奥行きが 0 以下・非数でも NaN にならないよう、下限で丸める。
inline double CocDiameterMm(double focalLengthMm, double aperture, double focusDistanceM, double depthM) {
    const double focusMm = ClampFocusDistanceM(focalLengthMm, focusDistanceM) * dof_detail::kMmPerM;
    const double scale = focalLengthMm * focalLengthMm / (aperture * (focusMm - focalLengthMm));
    if (std::isinf(depthM) && depthM > 0.0) return scale;
    const double depthMm = (depthM > dof_detail::kMinDepthM ? depthM : dof_detail::kMinDepthM) * dof_detail::kMmPerM;
    return scale * std::abs(depthMm - focusMm) / depthMm;
}

// 過焦点距離[m]。ここにピントを合わせると、この距離の半分から先が無限遠まで許容の錯乱円に収まる。
inline double HyperfocalDistanceM(double focalLengthMm, double aperture, double circleOfConfusionMm) {
    return (focalLengthMm * focalLengthMm / (aperture * circleOfConfusionMm) + focalLengthMm) / dof_detail::kMmPerM;
}

// ピントが合って見える範囲の手前の限界[m]。錯乱円が circleMm 以下に収まる最も近い距離。
inline double NearLimitM(double focalLengthMm, double aperture, double focusDistanceM, double circleMm) {
    const double hyperMm = HyperfocalDistanceM(focalLengthMm, aperture, circleMm) * dof_detail::kMmPerM;
    const double focusMm = ClampFocusDistanceM(focalLengthMm, focusDistanceM) * dof_detail::kMmPerM;
    return focusMm * (hyperMm - focalLengthMm) / (hyperMm + focusMm - 2.0 * focalLengthMm) / dof_detail::kMmPerM;
}

// ピントが合って見える範囲の奥の限界[m]。ピント距離が過焦点距離以上なら無限遠まで合うので無限大。
inline double FarLimitM(double focalLengthMm, double aperture, double focusDistanceM, double circleMm) {
    const double hyperMm = HyperfocalDistanceM(focalLengthMm, aperture, circleMm) * dof_detail::kMmPerM;
    const double focusMm = ClampFocusDistanceM(focalLengthMm, focusDistanceM) * dof_detail::kMmPerM;
    if (focusMm >= hyperMm) return std::numeric_limits<double>::infinity();
    return focusMm * (hyperMm - focalLengthMm) / (hyperMm - focusMm) / dof_detail::kMmPerM;
}

// センサー上の長さ cocMm[mm]を、出力画像の画素数に直す。フレームの幅が出力の幅に対応する。
inline float CocPixels(double cocMm, double frameWidthMm, double outputWidthPixels) {
    return static_cast<float>(cocMm / frameWidthMm * outputWidthPixels);
}

// ぼかしの半径の上限[画素]。出力の幅に対する割合から求め、1画素より小さくしない。
// 解像度が変わっても見た目の最大のぼけ方が同じになるよう、画素数でなく割合で決める。
inline float MaxCocRadiusPixels(std::uint32_t outputWidthPixels, float widthFraction) {
    return std::max(widthFraction * static_cast<float>(outputWidthPixels), 1.0f);
}

// 近景のぼけの最大値を求めるタイルの1辺(半解像度の画素)。ぼかしの半径の上限(DepthOfFieldMaxRadiusPixels)を
// 半解像度の画素に直した値以上であること。周囲 3x3 のタイルの最大を見れば、その画素へ届く近景のぼけが全部分かる。
constexpr std::uint32_t kDepthOfFieldTileSize = 16;

// 半解像度の画素数を覆うタイルの数(端の欠けたタイルも1つに数える)。
constexpr std::uint32_t DepthOfFieldTileCount(std::uint32_t halfPixels) {
    return (halfPixels + kDepthOfFieldTileSize - 1) / kDepthOfFieldTileSize;
}

// 実際に使うぼかしの半径の上限[フル解像度の画素]。割合から求めた上限(MaxCocRadiusPixels)を、
// タイル2枚ぶん(フル解像度でタイル1枚の半径)でも頭打ちにする。
inline float DepthOfFieldMaxRadiusPixels(std::uint32_t outputWidthPixels, float widthFraction) {
    return std::min(MaxCocRadiusPixels(outputWidthPixels, widthFraction), 2.0f * static_cast<float>(kDepthOfFieldTileSize));
}

// 円盤の中で拾う点の数。極端な値を許さない(粗すぎる・重すぎるのを防ぐ)。
constexpr std::uint32_t kDepthOfFieldMinSamples = 8;
constexpr std::uint32_t kDepthOfFieldMaxSamples = 64;
constexpr std::uint32_t ClampDepthOfFieldSamples(int samples) {
    return static_cast<std::uint32_t>(
        std::clamp(samples, static_cast<int>(kDepthOfFieldMinSamples), static_cast<int>(kDepthOfFieldMaxSamples)));
}

// 深度バッファの値(Reversed-Z。遠方面が 0)から、視線方向の奥行き[m]を求める。invViewProj は、ジッタなしの viewProj の
// 逆行列(row-vector 慣習の row-major、16 要素)。ndcX・ndcY は画素の中心の NDC。逆行列を掛けた w は、クリップ空間の w
// (視線方向の距離)の逆数になる。深度が 0 以下(空・遠方面)は無限大。
inline double LinearDepthM(const float (&invViewProj)[16], double ndcX, double ndcY, double depth) {
    if (!(depth > 0.0)) return std::numeric_limits<double>::infinity();
    const double w = ndcX * invViewProj[3] + ndcY * invViewProj[7] + depth * invViewProj[11] + invViewProj[15];
    return 1.0 / std::max(std::abs(w), 1e-9);
}

} // namespace rs::render

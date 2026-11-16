#include "PhotoAutoIso.h"

#include <algorithm>
#include <cmath>

#include "PhotoCamera.h"

namespace rs::app {

namespace {

// 中央重みのガウスの標準偏差(格子の中心から端までを 1 とした距離)。端(1.0)で重みが約 0.14、角で約 0.02。
constexpr double kMeteringSigma = 0.5;

bool IsPositiveFinite(double value) { return std::isfinite(value) && value > 0.0; }

} // namespace

std::optional<double> MeteredLuminance(std::span<const float> logLuminance, std::size_t width, std::size_t height) {
    if (width == 0 || height == 0 || logLuminance.size() != width * height) return std::nullopt;
    double weightedSum = 0.0;
    double weightSum = 0.0;
    for (std::size_t y = 0; y < height; ++y) {
        const double v = (static_cast<double>(y) + 0.5) / static_cast<double>(height) * 2.0 - 1.0;
        for (std::size_t x = 0; x < width; ++x) {
            const float value = logLuminance[y * width + x];
            if (!std::isfinite(value)) return std::nullopt;
            const double u = (static_cast<double>(x) + 0.5) / static_cast<double>(width) * 2.0 - 1.0;
            const double weight = std::exp(-(u * u + v * v) / (2.0 * kMeteringSigma * kMeteringSigma));
            weightedSum += weight * value;
            weightSum += weight;
        }
    }
    return std::exp2(weightedSum / weightSum);
}

double AutoIsoForLuminance(double luminance, double aperture, double shutterSeconds, const AutoIsoParams& params) {
    if (std::isnan(luminance) || !IsPositiveFinite(aperture) || !IsPositiveFinite(shutterSeconds) ||
        !IsPositiveFinite(params.tonemapExposure) || !IsPositiveFinite(params.targetBrightness))
        return kDefaultIso;
    if (!(luminance > 0.0)) return kAutoIsoMax;
    // 画面の明るさ luminance * 露出 * m が目標になる倍率 m。m = (ISO/基準ISO) * 絞りとシャッターの倍率 なので、ISO は m を
    // 絞りとシャッターの倍率(ISO を基準にした PhotoExposureMultiplier)で割って基準ISO を掛ける。
    const double multiplier = params.targetBrightness / (luminance * params.tonemapExposure);
    const double iso = kDefaultIso * multiplier / PhotoExposureMultiplier(kDefaultIso, aperture, shutterSeconds);
    if (!std::isfinite(iso)) return kAutoIsoMax;
    return std::clamp(iso, kAutoIsoMin, kAutoIsoMax);
}

void AutoIsoState::Reset() {
    luminance_.reset();
    log2Iso_ = std::log2(kDefaultIso);
}

double AutoIsoState::Update(double dtSeconds, std::optional<double> measuredLuminance, double aperture,
                            double shutterSeconds, const AutoIsoParams& params) {
    if (measuredLuminance) luminance_ = measuredLuminance;
    if (!luminance_) return Iso();
    const double target = std::log2(AutoIsoForLuminance(*luminance_, aperture, shutterSeconds, params));
    // 対数の空間で指数的に追う。時定数が 0 以下か、時間が不正なら、目標へ即座に合わせる。
    const double rate = (params.followSeconds > 0.0 && std::isfinite(dtSeconds) && dtSeconds >= 0.0)
                            ? 1.0 - std::exp(-dtSeconds / params.followSeconds)
                            : 1.0;
    log2Iso_ += (target - log2Iso_) * rate;
    return Iso();
}

double AutoIsoState::Iso() const { return std::clamp(std::exp2(log2Iso_), kAutoIsoMin, kAutoIsoMax); }

std::optional<double> AutoIsoState::TargetIso(double aperture, double shutterSeconds, const AutoIsoParams& params) const {
    if (!luminance_) return std::nullopt;
    return AutoIsoForLuminance(*luminance_, aperture, shutterSeconds, params);
}

} // namespace rs::app

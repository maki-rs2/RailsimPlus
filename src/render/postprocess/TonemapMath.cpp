#include "TonemapMath.h"
#include "TonemapCoefficients.h"

#include "../../platform/EnvironmentVariable.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>
#include <string_view>

#include "../../platform/text/AsciiCase.h"
#include "../resource/RenderTuning.h"

namespace rs::render {

namespace {

// tonemap 設定値と TonemapMode の対応(列挙の並びと同じ順)。
constexpr std::array<std::string_view, kTonemapModeCount> kTonemapModeNames = {
    "aces", "off", "hable", "reinhard", "neutral", "agx", "triplesection", "acesfitted"};
static_assert(static_cast<std::size_t>(TonemapMode::AcesFitted) + 1 == kTonemapModeCount, "列挙と名前の表の件数");

constexpr float kSrgbLinearThreshold = 0.04045f;
constexpr float kSrgbLinearScale = 12.92f;
constexpr float kSrgbOffset = 0.055f;
constexpr float kSrgbGamma = 2.4f;

// 逆算(InverseTonemapChannel)の探索範囲の上端と二分探索の回数。上端は露出を掛けても背景色の
// HDR値が収まる十分大きな値で、回数は float の仮数部(24ビット)を使い切る余裕を見た値。
constexpr float kInverseTonemapSearchMax = 1024.0f;
constexpr int kInverseTonemapIterations = 48;

using Rgb = std::array<float, 3>;

float Saturate(float value) {
    // NaNは0へ落とす(HLSLのsaturateと同じ)。
    if (!(value > 0.0f)) return 0.0f;
    return std::min(value, 1.0f);
}

float Aces(float x) {
    return (x * (kAcesA * x + kAcesB)) / (x * (kAcesC * x + kAcesD) + kAcesE);
}

float Hable(float x) {
    const float curve = (x * (kHableA * x + kHableC * kHableB) + kHableD * kHableE) /
                            (x * (kHableA * x + kHableB) + kHableD * kHableF) -
                        kHableE / kHableF;
    return curve * kHableScale;
}

float Reinhard(float x) {
    return x / (1.0f + x);
}

float SmoothStep(float edge0, float edge1, float x) {
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float TripleSection(float x) {
    const float p = kTripleSectionMaxBrightness;
    const float a = kTripleSectionContrast;
    const float m = kTripleSectionLinearStart;
    const float l0 = ((p - m) * kTripleSectionLinearLength) / a;
    const float s0 = m + l0;
    const float s1 = m + a * l0;
    const float c2 = (a * p) / (p - s1);
    const float cp = -c2 / p;
    const float w0 = 1.0f - SmoothStep(0.0f, m, x);
    const float w2 = x >= s0 ? 1.0f : 0.0f;
    const float w1 = 1.0f - w0 - w2;
    const float toe = m * std::pow(x / m, kTripleSectionBlackTightness) + kTripleSectionPedestal;
    const float shoulder = p - (p - s1) * std::exp(cp * (x - s0));
    const float linear = m + a * (x - m);
    return toe * w0 + linear * w1 + shoulder * w2;
}

Rgb Neutral(Rgb c) {
    const float minChannel = std::min({c[0], c[1], c[2]});
    const float offset =
        minChannel < kNeutralToeEnd ? minChannel - minChannel * minChannel / (4.0f * kNeutralOffset) : kNeutralOffset;
    for (float& v : c) v -= offset;
    const float peak = std::max({c[0], c[1], c[2]});
    if (peak < kNeutralStartCompression) return c;
    const float d = 1.0f - kNeutralStartCompression;
    const float newPeak = 1.0f - d * d / (peak + d - kNeutralStartCompression);
    const float g = 1.0f - 1.0f / (kNeutralDesaturation * (peak - newPeak) + 1.0f);
    for (float& v : c) v = v * (newPeak / peak) * (1.0f - g) + newPeak * g;
    return c;
}

Rgb Multiply(const float m[3][3], const Rgb& v) {
    Rgb out = {};
    for (int r = 0; r < 3; ++r) out[r] = m[r][0] * v[0] + m[r][1] * v[1] + m[r][2] * v[2];
    return out;
}

Rgb Agx(const Rgb& color) {
    Rgb x = Multiply(kAgxInset, color);
    for (float& v : x) {
        // 0以下は対数が取れないので、範囲の下端へ寄せる。
        const float ev = v > 0.0f ? std::log2(v) : kAgxMinEv;
        const float t = (std::clamp(ev, kAgxMinEv, kAgxMaxEv) - kAgxMinEv) / (kAgxMaxEv - kAgxMinEv);
        const float t2 = t * t;
        const float t4 = t2 * t2;
        v = kAgxPolynomial[0] * t4 * t2 + kAgxPolynomial[1] * t4 * t + kAgxPolynomial[2] * t4 +
            kAgxPolynomial[3] * t2 * t + kAgxPolynomial[4] * t2 + kAgxPolynomial[5] * t + kAgxPolynomial[6];
    }
    Rgb out = Multiply(kAgxOutset, x);
    for (float& v : out) v = std::pow(std::max(v, 0.0f), kAgxDisplayGamma);
    return out;
}

float AcesRrtOdtFit(float v) {
    const float a = v * (v + kAcesFittedA0) - kAcesFittedA1;
    const float b = v * (kAcesFittedB0 * v + kAcesFittedB1) + kAcesFittedB2;
    return a / b;
}

Rgb AcesFitted(const Rgb& color) {
    Rgb x = Multiply(kAcesFittedInput, {color[0] / kAcesFittedExposureScale, color[1] / kAcesFittedExposureScale,
                                        color[2] / kAcesFittedExposureScale});
    for (float& v : x) v = AcesRrtOdtFit(v);
    return Multiply(kAcesFittedOutput, x);
}

Rgb PerChannel(const Rgb& c, float (*curve)(float)) {
    return {curve(c[0]), curve(c[1]), curve(c[2])};
}

} // namespace

float SrgbToLinear(float srgb) {
    if (srgb <= kSrgbLinearThreshold) return srgb / kSrgbLinearScale;
    return std::pow((srgb + kSrgbOffset) / (1.0f + kSrgbOffset), kSrgbGamma);
}

void TonemapColor(const TonemapParams& params, const float linearRgb[3], float out[3]) {
    Rgb c = {linearRgb[0], linearRgb[1], linearRgb[2]};
    if (params.mode != TonemapMode::Off) {
        for (float& v : c) v = std::max(v * params.exposure, 0.0f);
    }
    switch (params.mode) {
    case TonemapMode::Off: break;
    case TonemapMode::Aces: c = PerChannel(c, Aces); break;
    case TonemapMode::Hable: c = PerChannel(c, Hable); break;
    case TonemapMode::Reinhard: c = PerChannel(c, Reinhard); break;
    case TonemapMode::Neutral: c = Neutral(c); break;
    case TonemapMode::Agx: c = Agx(c); break;
    case TonemapMode::TripleSection: c = PerChannel(c, TripleSection); break;
    case TonemapMode::AcesFitted: c = AcesFitted(c); break;
    }
    for (int i = 0; i < 3; ++i) out[i] = Saturate(c[i]);
}

float TonemapChannel(const TonemapParams& params, float linearValue) {
    const float gray[3] = {linearValue, linearValue, linearValue};
    float out[3] = {};
    TonemapColor(params, gray, out);
    return out[0];
}

float InverseTonemapChannel(const TonemapParams& params, float targetLinear) {
    float low = 0.0f;
    float high = kInverseTonemapSearchMax;
    for (int i = 0; i < kInverseTonemapIterations; ++i) {
        const float mid = 0.5f * (low + high);
        if (TonemapChannel(params, mid) < targetLinear) {
            low = mid;
        } else {
            high = mid;
        }
    }
    return high;
}

TonemapMode TonemapModeFromEnvValue(const char* value) {
    if (value == nullptr) return TonemapMode::Agx;
    const std::string text = rs::platform_text::ToAsciiLower(value);
    for (std::size_t i = 0; i < kTonemapModeNames.size(); ++i) {
        if (text == kTonemapModeNames[i]) return static_cast<TonemapMode>(i);
    }
    return TonemapMode::Agx;
}

const char* TonemapModeName(TonemapMode mode) {
    const std::size_t index = static_cast<std::size_t>(mode);
    return kTonemapModeNames[index < kTonemapModeCount ? index : static_cast<std::size_t>(TonemapMode::Agx)].data();
}

float TonemapExposureFromTuning(const RenderTuning& tuning) {
    // 負数・NaN・無限大は既定の1.0へ戻す(露出0は真っ黒になるだけなので許す)。
    return (std::isfinite(tuning.tonemapExposure) && tuning.tonemapExposure >= 0.0f) ? tuning.tonemapExposure : 1.0f;
}

TonemapParams MakeTonemapParams(const RenderTuning& tuning, const char* settingValue) {
    TonemapParams params;
    params.mode = TonemapModeFromEnvValue(settingValue);
    params.exposure = TonemapExposureFromTuning(tuning);
    return params;
}

TonemapParams LoadTonemapParams() {
    return MakeTonemapParams(RenderTuning::Embedded(), nullptr);
}

} // namespace rs::render

#include "PhotoCamera.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <numbers>
#include <utility>

#include "CameraView.h"
#include "../../core/StepMath.h"
#include "../workspace/CameraScreenPresentation.h"

namespace rs::app {
namespace {
using workspace::CameraScreenParameter;

std::optional<double> ParsePositive(std::string_view text) {
    double value = 0.0;
    const std::from_chars_result result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || !std::isfinite(value) || value <= 0.0) {
        return std::nullopt;
    }
    return value;
}

std::optional<double> ParseShutterSeconds(std::string_view text) {
    const std::size_t slash = text.find('/');
    if (slash == std::string_view::npos) return ParsePositive(text);
    const std::optional<double> numerator = ParsePositive(text.substr(0, slash));
    const std::optional<double> denominator = ParsePositive(text.substr(slash + 1));
    if (!numerator || !denominator) return std::nullopt;
    return *numerator / *denominator;
}
} // namespace

std::optional<double> ParsePhotoValue(CameraScreenParameter parameter, std::string_view text) {
    return parameter == CameraScreenParameter::ShutterSpeed ? ParseShutterSeconds(text) : ParsePositive(text);
}

PhotoCamera MakePhotoCamera(const workspace::CameraScreenPresentation& screen) {
    PhotoCamera camera;
    const auto read = [&screen](CameraScreenParameter parameter, double& target) {
        const std::size_t index = static_cast<std::size_t>(parameter);
        if (index >= screen.fields_.size()) return;
        if (const std::optional<double> value = ParsePhotoValue(parameter, screen.fields_[index].text_)) target = *value;
    };
    read(CameraScreenParameter::FocalLength, camera.focalLengthMm);
    // 焦点距離は連続で動かすので、丸める前の値があればそれを使う(表示は整数 mm)。
    const std::size_t focalIndex = static_cast<std::size_t>(CameraScreenParameter::FocalLength);
    if (focalIndex < screen.fields_.size() && screen.fields_[focalIndex].exactValue_) {
        camera.focalLengthMm = *screen.fields_[focalIndex].exactValue_;
    }
    read(CameraScreenParameter::Aperture, camera.aperture);
    read(CameraScreenParameter::ShutterSpeed, camera.shutterSeconds);
    read(CameraScreenParameter::Iso, camera.iso);
    const std::size_t isoIndex = static_cast<std::size_t>(CameraScreenParameter::Iso);
    camera.isoAuto = isoIndex < screen.fields_.size() && screen.fields_[isoIndex].IsAuto();
    read(CameraScreenParameter::FocusDistance, camera.focusDistanceM);
    return camera;
}

double PhotoEffectiveIso(const PhotoCamera& camera, std::optional<double> autoIso) {
    return camera.isoAuto && autoIso ? *autoIso : camera.iso;
}

std::string NearestNumericPhotoStep(CameraScreenParameter parameter, const std::vector<std::string>& steps, double value) {
    std::vector<double> values;
    std::vector<const std::string*> texts;
    for (const std::string& text : steps) {
        if (const std::optional<double> parsed = ParsePhotoValue(parameter, text)) {
            values.push_back(*parsed);
            texts.push_back(&text);
        }
    }
    const int index = rs::core::NearestLogStepIndex(values, value);
    return index < 0 ? std::string() : *texts[static_cast<std::size_t>(index)];
}

double PhotoExposureMultiplier(double iso, double aperture, double shutterSeconds) {
    const auto valid = [](double value) { return std::isfinite(value) && value > 0.0; };
    if (!valid(iso) || !valid(aperture) || !valid(shutterSeconds)) return 1.0;
    constexpr double kMaxMultiplier = 4096.0;
    const double apertureRatio = kDefaultAperture / aperture;
    const double multiplier = (iso / kDefaultIso) * (shutterSeconds / kDefaultShutterSeconds) * apertureRatio * apertureRatio;
    if (!std::isfinite(multiplier)) return multiplier > 0.0 ? kMaxMultiplier : 1.0;
    return std::clamp(multiplier, 1.0 / kMaxMultiplier, kMaxMultiplier);
}

PhotoFrameSize PhotoFrameForAspect(double aspect) {
    if (!std::isfinite(aspect) || aspect <= 0.0) return PhotoFrameSize{};
    // 縦長は、横長として求めた枠の幅と高さを入れ替える(縦横比の逆数で同じ規則を使うため)。
    const double landscape = aspect < 1.0 ? 1.0 / aspect : aspect;
    PhotoFrameSize frame;
    if (landscape >= kPhotoLongSideMm / kPhotoShortSideMm) {
        frame = {kPhotoLongSideMm, kPhotoLongSideMm / landscape}; // 幅が 36 mm いっぱい
    } else {
        frame = {kPhotoShortSideMm * landscape, kPhotoShortSideMm}; // 高さが 24 mm いっぱい
    }
    if (aspect < 1.0) std::swap(frame.widthMm, frame.heightMm);
    return frame;
}

float FovYFromFrameHeight(double focalLengthMm, double frameHeightMm) {
    if (!std::isfinite(focalLengthMm) || !std::isfinite(frameHeightMm) || focalLengthMm <= 0.0 || frameHeightMm <= 0.0) {
        return kViewerFovYRadians;
    }
    return static_cast<float>(2.0 * std::atan(frameHeightMm * 0.5 / focalLengthMm));
}

float FovYFromFocalLength(double focalLengthMm, double aspect) {
    if (!std::isfinite(aspect) || aspect <= 0.0) return kViewerFovYRadians;
    return FovYFromFrameHeight(focalLengthMm, PhotoFrameForAspect(aspect).heightMm);
}

double FocalLengthFromFovY(double fovYRadians, double frameHeightMm) {
    if (!std::isfinite(fovYRadians) || !std::isfinite(frameHeightMm) || fovYRadians <= 0.0 ||
        fovYRadians >= std::numbers::pi || frameHeightMm <= 0.0) {
        return kDefaultFocalLengthMm;
    }
    return frameHeightMm * 0.5 / std::tan(fovYRadians * 0.5);
}

int NearestPhotoStepIndex(CameraScreenParameter parameter, const std::vector<std::string>& steps, double value) {
    std::vector<double> values;
    values.reserve(steps.size());
    for (const std::string& text : steps) {
        const std::optional<double> parsed = ParsePhotoValue(parameter, text);
        if (!parsed) return -1;
        values.push_back(*parsed);
    }
    return rs::core::NearestLogStepIndex(values, value);
}

namespace {
// 焦点距離欄の段の最小と最大。欄が連続で動く範囲になる。
std::optional<std::pair<double, double>> FocalLengthRange(const workspace::CameraScreenField& field) {
    if (field.steps_.empty()) return std::nullopt;
    const std::optional<double> low = ParsePositive(field.steps_.front());
    const std::optional<double> high = ParsePositive(field.steps_.back());
    if (!low || !high || *low >= *high) return std::nullopt;
    return std::pair{*low, *high};
}
} // namespace

bool SetFocalLengthMm(workspace::CameraScreenField& field, double focalMm) {
    const std::optional<std::pair<double, double>> range = FocalLengthRange(field);
    if (!range || !std::isfinite(focalMm)) return false;
    const double clamped = std::clamp(focalMm, range->first, range->second);
    if (!field.SetText(std::to_string(std::lround(clamped)))) return false;
    field.exactValue_ = clamped;
    return true;
}

bool StepFocalLengthByFov(workspace::CameraScreenField& field, double notches, double fovStepRadians,
                          double frameHeightMm) {
    if (!std::isfinite(notches) || notches == 0.0 || !std::isfinite(fovStepRadians)) return false;
    const std::optional<double> current = field.exactValue_ ? field.exactValue_ : ParsePositive(field.text_);
    if (!current) return false;
    constexpr double kFovMargin = 1e-3; // 視野角が 0 や π になって焦点距離が決まらなくなるのを避ける
    const double fov = 2.0 * std::atan(frameHeightMm * 0.5 / *current) - notches * fovStepRadians;
    const double next = FocalLengthFromFovY(std::clamp(fov, kFovMargin, std::numbers::pi - kFovMargin), frameHeightMm);
    if (!SetFocalLengthMm(field, next)) return false;
    return *field.exactValue_ != *current;
}

std::optional<float> FieldOfViewOnFinderExit(double writtenFocalMm, double currentFocalMm, float minFovYRadians,
                                             float maxFovYRadians) {
    if (currentFocalMm == writtenFocalMm) return std::nullopt;
    return std::clamp(FovYFromFrameHeight(currentFocalMm, kNormalViewFrameHeightMm), minFovYRadians, maxFovYRadians);
}

} // namespace rs::app

#include "CameraScreenPresentation.h"

#include <charconv>
#include <algorithm>
#include <cmath>
#include <string_view>

#include "CameraSevenSegment.h"

namespace rs::app::workspace {
void CameraScreenPresentation::RequestPhoto() {
    if (active_) photoRequested_ = true;
}

bool CameraScreenPresentation::BeginPhotoCapture(bool otherCaptureRequested) {
    if (!active_ || !photoRequested_ || photoCapturing_) return false;
    if (!photoReady_) {
        photoReady_ = true;
        return false;
    }
    if (otherCaptureRequested) return false;
    photoCapturing_ = true;
    return true;
}

namespace {
// 1マスの幅を半マス単位で数える(通常のマスは2、小数点と斜線は1)。
constexpr int kNormalCellUnits = 2;
constexpr int kNarrowCellUnits = 1;

bool IsNarrowCharacter(char character) { return character == '.' || character == '/'; }

int DisplayUnits(std::string_view display) {
    int units = 0;
    for (const char character : display) units += IsNarrowCharacter(character) ? kNarrowCellUnits : kNormalCellUnits;
    return units;
}
} // namespace

void CameraScreenField::RefreshDigits() {
    // 段のどの値でも同じ幅になるよう、いちばん幅の広い値に合わせて先頭を空白のマスで埋める(右寄せ)。
    // 値が変わってもマスの数と両脇の矢印の位置が動かない。
    // 自動のときは、7セグで書けない「AUTO」の代わりに決まった値の数字を出す。
    const std::string shown = IsAuto() ? (autoDisplay_.empty() ? std::string(kCameraScreenAutoFallbackText) : autoDisplay_) : text_;
    std::string display = prefix_ + shown;
    int targetUnits = DisplayUnits(display);
    for (const std::string& step : steps_) targetUnits = std::max(targetUnits, DisplayUnits(prefix_ + step));
    int padUnits = targetUnits - DisplayUnits(display);
    std::string padding;
    for (; padUnits >= kNormalCellUnits; padUnits -= kNormalCellUnits) padding += ' ';
    // 半マス分の端数は、幅の狭い空白のマスで埋める。
    const bool narrowPad = padUnits == kNarrowCellUnits;
    display = padding + (narrowPad ? std::string(1, ' ') : std::string()) + display;
    const std::size_t narrowPadIndex = padding.size();
    digits_.resize(display.size());
    for (std::size_t i = 0; i < display.size(); ++i) {
        CameraScreenDigit& digit = digits_[i];
        digit.narrow_ = IsNarrowCharacter(display[i]) || (narrowPad && i == narrowPadIndex);
        const std::uint16_t mask = CameraSevenSegmentMask(display[i]);
        // 7本のセグメントに、小数点と斜線を加えた表示部品。
        constexpr int kSegmentCount = 9;
        digit.segments_.resize(kSegmentCount);
        for (int segment = 0; segment < kSegmentCount; ++segment) {
            digit.segments_[segment] = {segment, (mask & (1u << segment)) != 0};
        }
    }
}

bool CameraScreenField::Wheel(double delta) {
    if (!std::isfinite(delta) || delta == 0.0 || steps_.empty()) return false;
    const int next = step_ + (delta < 0.0 ? 1 : -1);
    if (next < 0 || static_cast<std::size_t>(next) >= steps_.size()) return false;
    step_ = next;
    exactValue_.reset();
    text_ = steps_[step_];
    draft_ = text_;
    RefreshDigits();
    return true;
}

bool CameraScreenField::SetAutoDisplay(std::string_view value) {
    if (autoDisplay_ == value) return false;
    autoDisplay_ = value;
    if (IsAuto()) RefreshDigits();
    return true;
}

bool CameraScreenField::SetText(std::string_view value) {
    std::size_t widest = 0;
    for (const std::string& step : steps_) widest = std::max(widest, step.size());
    if (value.empty() || value.size() > widest) return false;
    if (IsCameraScreenAutoText(value)) {
        // 「AUTO」は段に持つ欄だけが受け付ける。大文字小文字は揃えて持つ。
        if (std::find(steps_.begin(), steps_.end(), kCameraScreenAutoText) == steps_.end()) return false;
        value = kCameraScreenAutoText;
    }
    exactValue_.reset();
    text_ = value;
    draft_ = text_;
    const auto match = std::find(steps_.begin(), steps_.end(), text_);
    if (match != steps_.end()) step_ = static_cast<int>(match - steps_.begin());
    RefreshDigits();
    return true;
}

bool CameraScreenPresentation::SetResolution(std::string_view index) {
    int value = -1;
    const std::from_chars_result result = std::from_chars(index.data(), index.data() + index.size(), value);
    if (result.ec != std::errc{} || result.ptr != index.data() + index.size() || value < 0 ||
        static_cast<std::size_t>(value) >= resolutions_.size()) return false;
    resolution_ = value;
    return true;
}

void CameraScreenPresentation::Layout(double screenWidth, double screenHeight, const CameraFinderRect& available) {
    if (resolution_ < 0 || static_cast<std::size_t>(resolution_) >= resolutions_.size()) return;
    const CameraScreenResolution& resolution = resolutions_[resolution_];
    const CameraFinderRect frame = FitCameraFinder(available, resolution.width_, resolution.height_);
    if (frame != finder_) focusShown_ = false;
    finder_ = frame;
    const double right = frame.left_ + frame.width_;
    const double bottom = frame.top_ + frame.height_;
    masks_ = {{0.0, 0.0, screenWidth, frame.top_},
              {0.0, bottom, screenWidth, screenHeight - bottom},
              {0.0, frame.top_, frame.left_, frame.height_},
              {right, frame.top_, screenWidth - right, frame.height_}};
}

bool CameraScreenPresentation::FocusAt(double x, double y) {
    if (x < finder_.left_ || y < finder_.top_ || x > finder_.left_ + finder_.width_ || y > finder_.top_ + finder_.height_) {
        return false;
    }
    focusShown_ = true;
    focusX_ = x;
    focusY_ = y;
    return true;
}
}

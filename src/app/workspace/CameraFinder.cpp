#include "CameraFinder.h"

#include <algorithm>
#include <cmath>

namespace rs::app::workspace {
CameraFinderRect NormalizeCameraFinder(const CameraFinderRect& finder, double screenWidth, double screenHeight) {
    if (!std::isfinite(screenWidth) || !std::isfinite(screenHeight) || screenWidth <= 0.0 || screenHeight <= 0.0) return {};
    return {finder.left_ / screenWidth, finder.top_ / screenHeight, finder.width_ / screenWidth, finder.height_ / screenHeight};
}

CameraPhotoCrop CalculateCameraPhotoCrop(const CameraFinderRect& normalized, std::uint32_t width, std::uint32_t height) {
    if (!std::isfinite(normalized.left_) || !std::isfinite(normalized.top_) ||
        !std::isfinite(normalized.width_) || !std::isfinite(normalized.height_) ||
        normalized.width_ <= 0.0 || normalized.height_ <= 0.0) return {};
    const auto edge = [](double value, std::uint32_t size) {
        return static_cast<std::uint32_t>(std::round(std::clamp(value, 0.0, 1.0) * size));
    };
    const std::uint32_t left = edge(normalized.left_, width);
    const std::uint32_t top = edge(normalized.top_, height);
    const std::uint32_t right = edge(normalized.left_ + normalized.width_, width);
    const std::uint32_t bottom = edge(normalized.top_ + normalized.height_, height);
    return {left, top, right - left, bottom - top};
}

CameraFinderRect FitCameraFinder(const CameraFinderRect& available, double outputWidth, double outputHeight) {
    if (!std::isfinite(available.left_) || !std::isfinite(available.top_) ||
        !std::isfinite(available.width_) || !std::isfinite(available.height_) ||
        !std::isfinite(outputWidth) || !std::isfinite(outputHeight) ||
        available.width_ <= 0.0 || available.height_ <= 0.0 || outputWidth <= 0.0 || outputHeight <= 0.0) return {};
    const double scale = std::min(available.width_ / outputWidth, available.height_ / outputHeight);
    const double width = outputWidth * scale;
    const double height = outputHeight * scale;
    return {available.left_ + (available.width_ - width) * 0.5,
            available.top_ + (available.height_ - height) * 0.5, width, height};
}
}

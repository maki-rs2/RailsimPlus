#pragma once
#include <cstdint>

namespace rs::app::workspace {
// 左上と幅・高さは同じ画面座標のpxで受け渡す。
struct CameraFinderRect {
    double left_ = 0.0;
    double top_ = 0.0;
    double width_ = 0.0;
    double height_ = 0.0;
    bool operator==(const CameraFinderRect&) const = default;
};

CameraFinderRect FitCameraFinder(const CameraFinderRect& available, double outputWidth, double outputHeight);
CameraFinderRect NormalizeCameraFinder(const CameraFinderRect& finder, double screenWidth, double screenHeight);
struct CameraPhotoCrop {
    std::uint32_t left = 0, top = 0, width = 0, height = 0;
    bool operator==(const CameraPhotoCrop&) const = default;
};
CameraPhotoCrop CalculateCameraPhotoCrop(const CameraFinderRect& normalized, std::uint32_t width, std::uint32_t height);
}

#include "IconRectCrop.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace rs::app::plugin_browser {

bool IsFullIconRect(const float rect[4]) {
    return rect[0] == 0.0f && rect[1] == 0.0f && rect[2] == 1.0f && rect[3] == 1.0f;
}

rs::render::ImageData CropIconRect(const rs::render::ImageData& image, const float rect[4]) {
    if (!image.ok || image.width == 0 || image.height == 0 || IsFullIconRect(rect)) return image;

    const float width = static_cast<float>(image.width);
    const float height = static_cast<float>(image.height);
    const float left = std::clamp(rect[0], 0.0f, 1.0f) * width;
    const float top = std::clamp(rect[1], 0.0f, 1.0f) * height;
    const float right = std::clamp(rect[0] + rect[2], 0.0f, 1.0f) * width;
    const float bottom = std::clamp(rect[1] + rect[3], 0.0f, 1.0f) * height;

    const std::uint32_t x0 = static_cast<std::uint32_t>(std::floor(left));
    const std::uint32_t y0 = static_cast<std::uint32_t>(std::floor(top));
    const std::uint32_t x1 = std::min(image.width, static_cast<std::uint32_t>(std::ceil(right)));
    const std::uint32_t y1 = std::min(image.height, static_cast<std::uint32_t>(std::ceil(bottom)));
    if (x1 <= x0 || y1 <= y0) return image;

    constexpr std::size_t kBytesPerPixel = 4;
    rs::render::ImageData out;
    out.width = x1 - x0;
    out.height = y1 - y0;
    out.ok = true;
    out.pixels.resize(static_cast<std::size_t>(out.width) * out.height * kBytesPerPixel);
    const std::size_t rowBytes = static_cast<std::size_t>(out.width) * kBytesPerPixel;
    for (std::uint32_t y = 0; y < out.height; ++y) {
        const std::uint8_t* src = image.pixels.data() +
                                  (static_cast<std::size_t>(y0 + y) * image.width + x0) * kBytesPerPixel;
        std::memcpy(out.pixels.data() + static_cast<std::size_t>(y) * rowBytes, src, rowBytes);
    }
    return out;
}

} // namespace rs::app::plugin_browser

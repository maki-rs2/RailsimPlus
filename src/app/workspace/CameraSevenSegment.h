#pragma once

#include <array>
#include <cstdint>

namespace rs::app::workspace {
// a〜gは上・右上・右下・下・左下・左上・中央。続く2ビットは小数点と斜線。
struct CameraSevenSegmentPattern {
    char character_;
    std::uint16_t mask_;
};
inline constexpr std::array<CameraSevenSegmentPattern, 14> kCameraSevenSegmentPatterns{{
    {'0', 0x3f}, {'1', 0x06}, {'2', 0x5b}, {'3', 0x4f}, {'4', 0x66},
    {'5', 0x6d}, {'6', 0x7d}, {'7', 0x07}, {'8', 0x7f}, {'9', 0x6f},
    {'.', 0x80}, {'F', 0x71}, {'/', 0x100}, {' ', 0x00}
}};
std::uint16_t CameraSevenSegmentMask(char character);
}

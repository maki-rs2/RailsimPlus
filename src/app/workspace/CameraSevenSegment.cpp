#include "CameraSevenSegment.h"

namespace rs::app::workspace {
std::uint16_t CameraSevenSegmentMask(char character) {
    for (const CameraSevenSegmentPattern& pattern : kCameraSevenSegmentPatterns) {
        if (pattern.character_ == character) return pattern.mask_;
    }
    return 0;
}
}

#include "WheelJointTrigger.h"

#include <algorithm>
#include <cmath>

namespace rs::sim {

std::optional<double> DetectWheelJointCrossing(double distanceBefore, double distanceAfter,
                                               double jointIntervalMeters, double frameMs) {
    if (!(jointIntervalMeters > 0.0) || !std::isfinite(jointIntervalMeters)) return std::nullopt;
    if (!std::isfinite(distanceBefore) || !std::isfinite(distanceAfter)) return std::nullopt;

    const double f1 = distanceBefore / jointIntervalMeters;
    const double f2 = distanceAfter / jointIntervalMeters;
    // 0から遠い側への四捨五入。負の距離でも対称になる。
    const long long n1 = std::llround(f1);
    const long long n2 = std::llround(f2);
    if (n1 == n2) return std::nullopt;

    // 跨いだ境界(平均の継目番号)がフレームのどのあたりだったかで、鳴らす時刻を遅らせる。
    const double crossing = (0.5 * static_cast<double>(n1 + n2) - f1) / (f2 - f1);
    const double fraction = std::clamp(crossing, 0.0, 1.0);
    const double delayMs = fraction * std::min(frameMs, kWheelSoundLeadInMs);
    return kWheelSoundLeadInMs - delayMs;
}

} // namespace rs::sim

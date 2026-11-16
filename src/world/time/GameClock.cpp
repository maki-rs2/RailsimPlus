#include "GameClock.h"

#include <cmath>

namespace rs::world {

namespace {
constexpr double kSecondsPerDay = 86400.0;
}

void GameClock::Tick(double dtSeconds) {
    secondsOfDay_ += dtSeconds; // timeScale_は呼び出し側で適用済みの前提
    // secondsOfDay_ が負(逆行)にも86400超え(繰り上げ)にも対応する正規化。
    double daysDelta = std::floor(secondsOfDay_ / kSecondsPerDay);
    if (daysDelta != 0.0) {
        dayNumber_ += static_cast<int64_t>(daysDelta);
        secondsOfDay_ -= daysDelta * kSecondsPerDay;
    }
}

void GameClock::SetDateTime(int64_t dayNumber, double secondsOfDay) {
    double daysDelta = std::floor(secondsOfDay / kSecondsPerDay);
    dayNumber_ = dayNumber + static_cast<int64_t>(daysDelta);
    secondsOfDay_ = secondsOfDay - daysDelta * kSecondsPerDay;
}

double NextAbsoluteSecondsAt(double absoluteNowSeconds, int hour, int minute, int second) {
    double timeOfDay = std::fmod(
        static_cast<double>(hour) * 3600.0 + static_cast<double>(minute) * 60.0 +
            static_cast<double>(second),
        kSecondsPerDay);
    if (timeOfDay < 0.0) timeOfDay += kSecondsPerDay;
    const double dayStart = std::floor(absoluteNowSeconds / kSecondsPerDay) * kSecondsPerDay;
    const double candidate = dayStart + timeOfDay;
    return candidate < absoluteNowSeconds ? candidate + kSecondsPerDay : candidate;
}

} // namespace rs::world

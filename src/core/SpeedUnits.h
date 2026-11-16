#pragma once

namespace rs::core {

// 速度の単位換算。1 m/s = 3.6 km/h(1時間=3600秒、1km=1000m)。
inline constexpr double kMetersPerSecondToKilometersPerHour = 3.6;
inline constexpr double kKilometersPerHourToMetersPerSecond = 1.0 / kMetersPerSecondToKilometersPerHour;

constexpr double KilometersPerHourToMetersPerSecond(double kmh) {
    return kmh * kKilometersPerHourToMetersPerSecond;
}

constexpr double MetersPerSecondToKilometersPerHour(double mps) {
    return mps * kMetersPerSecondToKilometersPerHour;
}

} // namespace rs::core

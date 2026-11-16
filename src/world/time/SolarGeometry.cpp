#include "SolarGeometry.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numbers>

#include "../../core/Math.h"

namespace rs::world {

double SeasonalPhaseRad(int dayOfYear) {
    return 2.0 * std::numbers::pi / 365.25 * (dayOfYear - 81);
}

double ForcedAbsoluteDays(EarthRevolutionMode mode) {
    return 365.0 * (static_cast<int>(mode) % 4) / 4.0 - 10.0;
}

int ResolveDayOfYear(const GameClock& clock, const SolarOverrides& overrides) {
    if (overrides.earthRevolutionMode != EarthRevolutionMode::Auto) {
        // RailSim II のabstime式の小数を切り捨てた日を年内日として使う。
        return static_cast<int>(ForcedAbsoluteDays(overrides.earthRevolutionMode));
    }
    return DayOfYear(clock.DayNumber());
}

double ResolveLocalHour(const GameClock& clock, const SolarOverrides& overrides) {
    // 経度・時差は扱わず、ゲーム内時刻12:00を太陽南中とする単純化モデル。
    // 自転の強制は RailSim II の「常に昼(正午扱い)/常に夜(深夜0時扱い)」に倣う。
    switch (overrides.earthRotationMode) {
    case EarthRotationMode::AlwaysDay: return 12.0;
    case EarthRotationMode::AlwaysNight: return 0.0;
    case EarthRotationMode::Auto: break;
    }
    return clock.SecondsOfDay() / 3600.0;
}

double HourAngleRad(double localHour) {
    return (localHour - 12.0) * (rs::core::kPi / 12.0);  // 15度/時
}

std::optional<double> ParseElevationOverrideDeg(const char* value) {
    const std::optional<double> parsed = ParseFiniteNumber(value);
    if (!parsed) return std::nullopt;
    return std::clamp(*parsed, -kMaxElevationDeg, kMaxElevationDeg);
}

double ElevationRadFromOverride(double elevationDeg) {
    return std::clamp(elevationDeg, -kMaxElevationDeg, kMaxElevationDeg) * rs::core::kDegToRad;
}

void DirectionFromElevation(double elevationRad, double azimuthRad, double& x, double& y, double& z) {
    const double horizontal = std::cos(elevationRad);
    x = horizontal * std::sin(azimuthRad);
    y = std::sin(elevationRad);
    z = horizontal * std::cos(azimuthRad);
}

std::optional<double> ParseFiniteNumber(const char* value) {
    if (value == nullptr) return std::nullopt;
    char* end = nullptr;
    const double parsed = std::strtod(value, &end);
    if (end == value) return std::nullopt;
    while (*end == ' ' || *end == '\t') ++end;
    if (*end != '\0' || !std::isfinite(parsed)) return std::nullopt;
    return parsed;
}

HorizontalDirection EquatorialToHorizontal(double latRad, double declRad, double hourAngleRad) {
    double sinElevation = std::sin(latRad) * std::sin(declRad) +
                          std::cos(latRad) * std::cos(declRad) * std::cos(hourAngleRad);
    sinElevation = std::clamp(sinElevation, -1.0, 1.0);
    const double elevationRad = std::asin(sinElevation);

    // 南を基準としたatan2形式の方位角(数値的に安定、象限判定不要)を求め、
    // 北基準(このプロジェクトの規約)へ180度シフトする。
    const double azimuthFromSouth = std::atan2(
        std::sin(hourAngleRad),
        std::cos(hourAngleRad) * std::sin(latRad) - std::tan(declRad) * std::cos(latRad));
    double azimuthRad = azimuthFromSouth + std::numbers::pi;
    if (azimuthRad >= 2.0 * std::numbers::pi) azimuthRad -= 2.0 * std::numbers::pi;
    if (azimuthRad < 0.0) azimuthRad += 2.0 * std::numbers::pi;

    HorizontalDirection result;
    result.elevationRad = elevationRad;
    result.azimuthRad = azimuthRad;
    const double horizontal = std::cos(elevationRad);
    result.x = horizontal * std::sin(azimuthRad);
    result.y = sinElevation;
    result.z = horizontal * std::cos(azimuthRad);
    return result;
}

} // namespace rs::world

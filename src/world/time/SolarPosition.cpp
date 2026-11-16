#include "SolarPosition.h"

#include "SolarGeometry.h"
#include "../../core/Math.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace rs::world {

SolarState ComputeSolarPosition(const GameClock& clock, double latitudeDeg,
                                 double axialInclinationDeg, const SolarOverrides& overrides) {
    const int dayOfYear = ResolveDayOfYear(clock, overrides);

    // 太陽赤緯(季節変化)。北半球夏至(day≈172)で最大、冬至(day≈355)で最小
    // となる簡易近似式(NOAA簡易式)。
    const double declRad = axialInclinationDeg * rs::core::kDegToRad * std::sin(SeasonalPhaseRad(dayOfYear));

    const double latRad = latitudeDeg * rs::core::kDegToRad;
    const double hourAngleRad = HourAngleRad(ResolveLocalHour(clock, overrides));
    const HorizontalDirection direction = EquatorialToHorizontal(latRad, declRad, hourAngleRad);

    SolarState state;
    state.elevationRad = direction.elevationRad;
    state.azimuthRad = direction.azimuthRad;
    state.directionToSunX = direction.x;
    state.directionToSunY = direction.y;
    state.directionToSunZ = direction.z;
    return state;
}

double ComputeDayAlpha(double directionToSunY, double nightThresholdSin) {
    return std::clamp((directionToSunY - nightThresholdSin) * 10.0, 0.0, 1.0);
}

std::optional<double> ParseSunElevationOverrideDeg(const char* value) {
    return ParseElevationOverrideDeg(value);
}

void ApplySunElevationOverride(SolarState& state, double elevationDeg) {
    state.elevationRad = ElevationRadFromOverride(elevationDeg);
    DirectionFromElevation(state.elevationRad, state.azimuthRad, state.directionToSunX, state.directionToSunY,
                           state.directionToSunZ);
}

} // namespace rs::world

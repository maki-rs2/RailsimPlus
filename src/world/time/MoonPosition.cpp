#include "MoonPosition.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numbers>

#include "SolarGeometry.h"
#include "../../core/Math.h"

namespace rs::world {

namespace {

// 白道(月の軌道面)の黄道に対する傾き[度](天文学の標準値)。昇交点の移動は扱わない。
constexpr double kLunarOrbitInclinationDeg = 5.14;
// RailSim II の太陽の公転周期[日](CEnvPlugin.cpp の REV_PER_DAY = 2π/365.2425)。
constexpr double kEarthRevolutionPeriodDays = 365.2425;
// RailSim II の太陽の公転角の位相ずれ[日](冬至基準。公転角 = (abstime + 10) × 2π / 365.2425)。
constexpr double kEarthRevolutionPhaseDays = 10.0;
constexpr double kSecondsPerDay = 86400.0;

// 月の等級の経験式(Allen, Astrophysical Quantities): m(α) = −12.73 + 0.026|α| + 4×10⁻⁹ α⁴(αは度)。
constexpr double kMoonMagnitudeAtFull = -12.73;
constexpr double kMoonMagnitudeLinear = 0.026;
constexpr double kMoonMagnitudeQuartic = 4.0e-9;
// 経験式の適用範囲の上限[度]。これを超える新月側は、この角度の値から180度で0へ線形につなぐ。
constexpr double kEmpiricalMaxPhaseDeg = 150.0;
constexpr double kNewMoonPhaseDeg = 180.0;

double MoonMagnitude(double phaseDeg) {
    const double a = std::fabs(phaseDeg);
    return kMoonMagnitudeAtFull + kMoonMagnitudeLinear * a + kMoonMagnitudeQuartic * a * a * a * a;
}

double EmpiricalBrightness(double phaseDeg) {
    return std::pow(10.0, -0.4 * (MoonMagnitude(phaseDeg) - MoonMagnitude(0.0)));
}

// 黄道座標(黄経lon・黄緯lat)→赤道座標の赤緯・赤経。epsは地軸傾斜(黄道と赤道の傾き)。
struct Equatorial {
    double declRad = 0.0;
    double rightAscensionRad = 0.0;
};

Equatorial EclipticToEquatorial(double lonRad, double latRad, double epsRad) {
    const double sinDecl = std::sin(latRad) * std::cos(epsRad) +
                           std::cos(latRad) * std::sin(epsRad) * std::sin(lonRad);
    Equatorial result;
    result.declRad = std::asin(std::clamp(sinDecl, -1.0, 1.0));
    result.rightAscensionRad = std::atan2(
        std::sin(lonRad) * std::cos(epsRad) - std::tan(latRad) * std::sin(epsRad), std::cos(lonRad));
    return result;
}

} // namespace

double MoonPhaseBrightness(double phaseAngleRad) {
    const double phaseDeg = std::clamp(phaseAngleRad / rs::core::kDegToRad, 0.0, kNewMoonPhaseDeg);
    if (phaseDeg <= kEmpiricalMaxPhaseDeg) return EmpiricalBrightness(phaseDeg);
    const double edge = EmpiricalBrightness(kEmpiricalMaxPhaseDeg);
    return edge * (kNewMoonPhaseDeg - phaseDeg) / (kNewMoonPhaseDeg - kEmpiricalMaxPhaseDeg);
}

MoonState ComputeMoonPosition(const GameClock& clock, double latitudeDeg, double axialInclinationDeg,
                              const MoonParams& params, const SolarOverrides& overrides) {
    const double localHour = ResolveLocalHour(clock, overrides);

    // 絶対時間[日]。公転の強制モードは RailSim II のabstime式の値をそのまま使う(太陽の季節と同じ扱い)。
    const double dayPart = overrides.earthRevolutionMode != EarthRevolutionMode::Auto
                               ? ForcedAbsoluteDays(overrides.earthRevolutionMode)
                               : static_cast<double>(clock.DayNumber());
    const double absoluteDays = dayPart + localHour * 3600.0 / kSecondsPerDay;

    // 離角(太陽から見た月の黄経の進み)。RailSim II の月の公転角から太陽の公転角を引いた値。
    const double elongationRad =
        2.0 * rs::core::kPi * ((absoluteDays + params.initialPhaseDays) / params.revolutionPeriodDays -
                     (absoluteDays + kEarthRevolutionPhaseDays) / kEarthRevolutionPeriodDays);

    const double epsRad = axialInclinationDeg * rs::core::kDegToRad;
    const double latRad = latitudeDeg * rs::core::kDegToRad;
    const double sunLonRad = SeasonalPhaseRad(ResolveDayOfYear(clock, overrides));
    const double moonLonRad = sunLonRad + elongationRad;
    const double moonLatRad = kLunarOrbitInclinationDeg * rs::core::kDegToRad * std::sin(moonLonRad);

    const Equatorial sun = EclipticToEquatorial(sunLonRad, 0.0, epsRad);
    const Equatorial moon = EclipticToEquatorial(moonLonRad, moonLatRad, epsRad);

    // 太陽の時角から、赤経の差だけ月は西へ遅れる(時角が増える)。
    const double sunHourAngleRad = HourAngleRad(localHour);
    const double moonHourAngleRad = sunHourAngleRad - (moon.rightAscensionRad - sun.rightAscensionRad);

    const HorizontalDirection moonDir = EquatorialToHorizontal(latRad, moon.declRad, moonHourAngleRad);
    // 位相角用の太陽の方向。月と同じ赤道座標の式で出し、2方向のなす角を黄道座標の幾何と一致させる。
    const HorizontalDirection sunDir = EquatorialToHorizontal(latRad, sun.declRad, sunHourAngleRad);

    const double cosAngle = std::clamp(
        moonDir.x * sunDir.x + moonDir.y * sunDir.y + moonDir.z * sunDir.z, -1.0, 1.0);

    MoonState state;
    state.directionToMoonX = moonDir.x;
    state.directionToMoonY = moonDir.y;
    state.directionToMoonZ = moonDir.z;
    state.elevationRad = moonDir.elevationRad;
    state.azimuthRad = moonDir.azimuthRad;
    state.phaseAngleRad = rs::core::kPi - std::acos(cosAngle);
    state.brightness = MoonPhaseBrightness(state.phaseAngleRad);
    return state;
}

std::optional<double> ParseMoonElevationOverrideDeg(const char* value) {
    return ParseElevationOverrideDeg(value);
}

void ApplyMoonElevationOverride(MoonState& state, double elevationDeg) {
    state.elevationRad = ElevationRadFromOverride(elevationDeg);
    DirectionFromElevation(state.elevationRad, state.azimuthRad, state.directionToMoonX, state.directionToMoonY,
                           state.directionToMoonZ);
}

std::optional<double> ParseMoonPhaseOverrideDeg(const char* value) {
    const std::optional<double> parsed = ParseFiniteNumber(value);
    if (!parsed) return std::nullopt;
    return std::clamp(*parsed, 0.0, kNewMoonPhaseDeg);
}

void ApplyMoonPhaseOverride(MoonState& state, double phaseDeg) {
    state.phaseAngleRad = std::clamp(phaseDeg, 0.0, kNewMoonPhaseDeg) * rs::core::kDegToRad;
    state.brightness = MoonPhaseBrightness(state.phaseAngleRad);
}

} // namespace rs::world

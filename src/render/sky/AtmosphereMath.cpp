#include "AtmosphereMath.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "AtmosphereParams.h"
#include "../../core/Math.h"

namespace rs::render::atmosphere {

namespace {

constexpr double kBottomRadius = kBottomRadiusKm;

// 0..1を、LUTの端のテクセル中心(0.5/解像度)の内側へ収める。逆はUnitFromSubUv。
double SubUvFromUnit(double unit, double resolution) {
    return (unit * (resolution - 1.0) + 0.5) / resolution;
}

double UnitFromSubUv(double subUv, double resolution) {
    return (subUv * resolution - 0.5) / (resolution - 1.0);
}

double HorizonBeta(double viewHeightKm) {
    return std::acos(SkyViewHorizonCos(viewHeightKm));
}

} // namespace

bool ShouldRegenerateSkyView(bool hasPrevious, const rs::core::Vec3& previousSun,
                             const rs::core::Vec3& currentSun, double thresholdDeg) {
    if (!hasPrevious) return true;
    // Sky-View LUTの中身は太陽の高度(sunDir.y)だけで決まり、方位は引くときの相対角で吸収される
    // ので、方位だけが動いても作り直さない。asinはy=±1付近で誤差が
    // 増幅されないようclampしてから求める(DirectionElevationDeg)。
    return std::abs(DirectionElevationDeg(currentSun) - DirectionElevationDeg(previousSun)) >= thresholdDeg;
}

double ObserverHeightKmFromCameraMeters(double cameraHeightMeters) {
    const double heightKm = kObserverHeightKm + std::max(cameraHeightMeters, 0.0) * 0.001;
    return std::min(heightKm, static_cast<double>(kObserverHeightMaxKm));
}

bool ShouldRegenerateForObserverHeight(double previousKm, double currentKm, double thresholdKm) {
    return std::abs(currentKm - previousKm) >= thresholdKm;
}

double DirectionElevationDeg(const rs::core::Vec3& direction) {
    const double length = std::sqrt(double(direction.x) * direction.x + double(direction.y) * direction.y +
                                    double(direction.z) * direction.z);
    const double sinElevation = length > 0.0 ? std::clamp(double(direction.y) / length, -1.0, 1.0) : 0.0;
    return std::asin(sinElevation) * 180.0 / rs::core::kPi;
}

bool ShouldRegenerateIblForMoon(const rs::core::Vec3& previousMoon, float previousRatio,
                                const rs::core::Vec3& currentMoon, float currentRatio, double thresholdDeg) {
    if (previousRatio <= 0.0f && currentRatio <= 0.0f) return false;
    // 0との行き来は、割合を求められないので無条件に作り直す。
    if (previousRatio <= 0.0f || currentRatio <= 0.0f) return true;
    if (std::abs(currentRatio - previousRatio) / previousRatio >= kMoonIblRegenerateRatioChange) return true;
    // IBLの環境ソースは方位にも依存するので、方向のなす角で見る(高度だけでなく)。
    const double dot = std::clamp(
        double(rs::core::Vec3::Dot(previousMoon.Normalized(), currentMoon.Normalized())), -1.0, 1.0);
    return std::acos(dot) * 180.0 / rs::core::kPi >= thresholdDeg;
}

double SkyViewHorizonCos(double viewHeightKm) {
    const double horizon =
        std::sqrt(std::max(0.0, viewHeightKm * viewHeightKm - kBottomRadius * kBottomRadius));
    return horizon / viewHeightKm;
}

bool SkyViewIntersectsGround(double viewZenithCos, double viewHeightKm) {
    return viewZenithCos < -SkyViewHorizonCos(viewHeightKm);
}

SkyViewDirection SkyViewDirectionFromUv(double u, double v, double viewHeightKm) {
    const double unitU = UnitFromSubUv(u, kSkyViewLutWidth);
    const double unitV = UnitFromSubUv(v, kSkyViewLutHeight);
    const double beta = HorizonBeta(viewHeightKm);
    const double zenithHorizonAngle = rs::core::kPi - beta;

    SkyViewDirection direction;
    if (unitV < 0.5) {
        double coord = 1.0 - 2.0 * unitV;
        coord = 1.0 - coord * coord;
        direction.viewZenithCos = std::cos(zenithHorizonAngle * coord);
    } else {
        double coord = unitV * 2.0 - 1.0;
        coord *= coord;
        direction.viewZenithCos = std::cos(zenithHorizonAngle + beta * coord);
    }
    direction.lightViewCos = -(unitU * unitU * 2.0 - 1.0);
    return direction;
}

void SkyViewUvFromDirection(const SkyViewDirection& direction, double viewHeightKm, double& u,
                            double& v) {
    const double beta = HorizonBeta(viewHeightKm);
    const double zenithHorizonAngle = rs::core::kPi - beta;
    const double zenithAngle = std::acos(std::clamp(direction.viewZenithCos, -1.0, 1.0));

    double unitV = 0.0;
    if (!SkyViewIntersectsGround(direction.viewZenithCos, viewHeightKm)) {
        double coord = 1.0 - zenithAngle / zenithHorizonAngle;
        coord = 1.0 - std::sqrt(std::max(coord, 0.0));
        unitV = coord * 0.5;
    } else {
        const double coord = (zenithAngle - zenithHorizonAngle) / beta;
        unitV = std::sqrt(std::max(coord, 0.0)) * 0.5 + 0.5;
    }
    const double unitU = std::sqrt(std::max(-direction.lightViewCos * 0.5 + 0.5, 0.0));

    u = SubUvFromUnit(unitU, kSkyViewLutWidth);
    v = SubUvFromUnit(unitV, kSkyViewLutHeight);
}

CubeDirection CubeFaceDirection(int face, double tx, double ty) {
    const double u = 2.0 * tx - 1.0;
    const double v = 2.0 * ty - 1.0;
    CubeDirection d;
    switch (face) {
    case 0: d = {1.0, -v, -u}; break;
    case 1: d = {-1.0, -v, u}; break;
    case 2: d = {u, 1.0, v}; break;
    case 3: d = {u, -1.0, -v}; break;
    case 4: d = {u, -v, 1.0}; break;
    default: d = {-u, -v, -1.0}; break;
    }
    const double length = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    return {d.x / length, d.y / length, d.z / length};
}

double IblSpecularMipRoughness(int mip, int mipCount) {
    if (mipCount <= 1) return 0.0;
    return static_cast<double>(std::clamp(mip, 0, mipCount - 1)) / static_cast<double>(mipCount - 1);
}

double NightAdaptationGain(double adaptationLuminance, double start, double exponent, double maxGain) {
    if (adaptationLuminance >= start) return 1.0;
    // E が0(または極小)でも発散しないよう、分母に下限を置く(IblShader.h の AdaptationGain と同じ値)。
    constexpr double kMinLuminance = 1e-12;
    const double gain = std::pow(start / std::max(adaptationLuminance, kMinLuminance), exponent);
    return std::clamp(gain, 1.0, std::max(maxGain, 1.0));
}

double ExposureAdaptationGain(double adaptationLuminance, double nightStart, double nightExponent, double dayStart,
                              double dayExponent, double maxGain) {
    return std::max(NightAdaptationGain(adaptationLuminance, nightStart, nightExponent, maxGain),
                    NightAdaptationGain(adaptationLuminance, dayStart, dayExponent, maxGain));
}

double NightAdaptationBoost(double sunElevationDeg, double boost) {
    const double t = std::clamp((kNightBoostStartSunElevationDeg - sunElevationDeg) /
                                    (kNightBoostStartSunElevationDeg - kNightBoostFullSunElevationDeg),
                                0.0, 1.0);
    return std::pow(std::max(boost, 1.0), t);
}

double AerialDistanceKmFromUnit(double unit, double maxDistanceKm) {
    const double clamped = std::clamp(unit, 0.0, 1.0);
    return maxDistanceKm * clamped * clamped;
}

double AerialUnitFromDistanceKm(double distanceKm, double maxDistanceKm) {
    if (maxDistanceKm <= 0.0) return 0.0;
    return std::sqrt(std::clamp(distanceKm / maxDistanceKm, 0.0, 1.0));
}

double AerialDepthWFromUnit(double unit, int sliceCount) {
    return SubUvFromUnit(std::clamp(unit, 0.0, 1.0), sliceCount);
}

double AerialUnitFromDepthW(double w, int sliceCount) {
    return std::clamp(UnitFromSubUv(w, sliceCount), 0.0, 1.0);
}

double AerialUnitFromTexelIndex(int index, int count) {
    return UnitFromSubUv((index + 0.5) / count, count);
}

double AerialAtlasU(double unitX, int slice, int tileSize, int sliceCount) {
    const double inTile = SubUvFromUnit(std::clamp(unitX, 0.0, 1.0), tileSize);
    return (slice + inTile) / sliceCount;
}

AerialCameraFrame MakeAerialCameraFrame(const rs::core::Mat4& view, float fovYRadians, float aspectRatio) {
    AerialCameraFrame frame;
    // row-vector 慣習の LookAt: 列がカメラのx/y/z軸(右・上・前)。
    for (int i = 0; i < 3; ++i) {
        frame.right[i] = view.m[i * 4 + 0];
        frame.up[i] = view.m[i * 4 + 1];
        frame.forward[i] = view.m[i * 4 + 2];
    }
    frame.tanHalfFovY = std::tan(fovYRadians * 0.5f);
    frame.tanHalfFovX = frame.tanHalfFovY * aspectRatio;
    return frame;
}

std::array<double, 3> AerialViewDirection(const AerialCameraFrame& frame, double unitX, double unitY) {
    const double x = (2.0 * unitX - 1.0) * frame.tanHalfFovX;
    const double y = (2.0 * unitY - 1.0) * frame.tanHalfFovY;
    std::array<double, 3> direction = {};
    double lengthSquared = 0.0;
    for (int i = 0; i < 3; ++i) {
        direction[i] = frame.forward[i] + x * frame.right[i] - y * frame.up[i];
        lengthSquared += direction[i] * direction[i];
    }
    const double length = std::sqrt(lengthSquared);
    for (double& v : direction) v /= length;
    return direction;
}

std::array<double, 3> AerialTransmittance(double observerRadiusKm, double viewZenithCos, double distanceKm,
                                          double aerosolOpticalDepth, int steps) {
    std::array<double, 3> opticalDepth = {0.0, 0.0, 0.0};
    if (distanceKm <= 0.0 || steps <= 0) return {1.0, 1.0, 1.0};
    const double mieExtinction = MieExtinctionPerKm(static_cast<float>(aerosolOpticalDepth));
    const double stepKm = distanceKm / steps;
    for (int i = 0; i < steps; ++i) {
        const double s = (i + 0.5) * stepKm;
        // 観測者から s だけ進んだ点の惑星中心からの距離(余弦定理)。
        const double radius = std::sqrt(observerRadiusKm * observerRadiusKm + s * s +
                                        2.0 * observerRadiusKm * s * viewZenithCos);
        const double height = radius - kBottomRadius;
        const double rayleighDensity = std::exp(-height / kRayleighScaleHeightKm);
        const double mieDensity = std::exp(-height / kMieScaleHeightKm);
        const double ozoneDensity =
            std::max(0.0, 1.0 - std::abs(height - kOzoneCenterHeightKm) / kOzoneHalfWidthKm);
        for (int c = 0; c < 3; ++c) {
            const double extinction = kRayleighScatteringPerKm[c] * rayleighDensity +
                                      mieExtinction * mieDensity + kOzoneAbsorptionPerKm[c] * ozoneDensity;
            opticalDepth[c] += extinction * stepKm;
        }
    }
    return {std::exp(-opticalDepth[0]), std::exp(-opticalDepth[1]), std::exp(-opticalDepth[2])};
}

} // namespace rs::render::atmosphere

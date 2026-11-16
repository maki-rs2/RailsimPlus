#include "HeightFogMath.h"

#include <algorithm>
#include <cmath>

namespace rs::render::height_fog {

double OpticalDepth(double density, double heightScale, double baseHeight, double cameraHeight, double pixelHeight,
                    double distance) {
    if (density <= 0.0 || heightScale <= 0.0 || distance <= 0.0) return 0.0;
    // 基準の高さからの高さを H で割った値(指数の引数)。
    const double a = std::clamp((cameraHeight - baseHeight) / heightScale, -kExponentLimit, kExponentLimit);
    const double b = std::clamp((pixelHeight - baseHeight) / heightScale, -kExponentLimit, kExponentLimit);
    const double slope = b - a;
    double meanDensityFactor = 0.0; // 視線に沿った exp(-x) の平均値
    if (std::fabs(slope) < kSmallSlope) {
        meanDensityFactor = std::exp(-(a + b) * 0.5);
    } else {
        meanDensityFactor = (std::exp(-a) - std::exp(-b)) / slope;
    }
    return std::max(density * distance * meanDensityFactor, 0.0);
}

double SunOpticalDepth(double density, double heightScale, double baseHeight, double cameraHeight,
                       double sinElevation) {
    if (density <= 0.0 || heightScale <= 0.0) return 0.0;
    const double a = std::clamp((cameraHeight - baseHeight) / heightScale, -kExponentLimit, kExponentLimit);
    return density * heightScale * std::exp(-a) / std::max(sinElevation, kMinSunSine);
}

double Transmittance(double opticalDepth) {
    return std::exp(-std::max(opticalDepth, 0.0));
}

} // namespace rs::render::height_fog

#include "EaseTable.h"

#include <algorithm>
#include <cmath>

namespace rs::legacy_plugin {

namespace {

constexpr int kBisectionSteps = 32;

struct Vec2d {
    double x = 0.0;
    double y = 0.0;
};

Vec2d StartTangent(double a) {
    if (a > 0.0) return {a, 0.0};
    if (a < 0.0) return {0.0, -a};
    return {0.0, 0.0};
}

Vec2d EndTangent(double b) {
    if (b > 0.0) return {0.0, b};
    if (b < 0.0) return {-b, 0.0};
    return {0.0, 0.0};
}

// (0,0)→(1,1) のエルミート曲線上の媒介変数 s の点。
Vec2d HermitePoint(double s, const Vec2d& t0, const Vec2d& t1) {
    const double s2 = s * s;
    const double s3 = s2 * s;
    const double h01 = -2.0 * s3 + 3.0 * s2;
    const double h10 = s3 - 2.0 * s2 + s;
    const double h11 = s3 - s2;
    return {h10 * t0.x + h01 + h11 * t1.x, h10 * t0.y + h01 + h11 * t1.y};
}

} // namespace

EaseTable::EaseTable(float startEase, float endEase) {
    const Vec2d t0 = StartTangent(startEase);
    const Vec2d t1 = EndTangent(endEase);
    table_[0] = 0.0f;
    table_[kDivisions] = 1.0f;
    for (int k = 1; k < kDivisions; ++k) {
        const double targetX = static_cast<double>(k) / kDivisions;
        double lo = 0.0;
        double hi = 1.0;
        for (int i = 0; i < kBisectionSteps; ++i) {
            const double mid = 0.5 * (lo + hi);
            if (HermitePoint(mid, t0, t1).x < targetX) lo = mid;
            else hi = mid;
        }
        table_[static_cast<size_t>(k)] = static_cast<float>(HermitePoint(0.5 * (lo + hi), t0, t1).y);
    }
}

float EaseTable::Eval(float v) const {
    const float clamped = std::clamp(v, 0.0f, 1.0f);
    const float scaled = clamped * static_cast<float>(kDivisions);
    const int index = static_cast<int>(scaled);
    if (index >= kDivisions) return table_[kDivisions];
    const float frac = scaled - static_cast<float>(index);
    const float a = table_[static_cast<size_t>(index)];
    const float b = table_[static_cast<size_t>(index) + 1];
    return std::lerp(a, b, frac);
}

} // namespace rs::legacy_plugin

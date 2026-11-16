//
// math.h - 簡易 4x4 行列とカメラ用数学ユーティリティ
//
#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>
#include <cstdint>

#include "Epsilon.h"

namespace rs::core {

// ---- 数学定数(円周率・角度換算)。末尾Fはfloat版 ----
inline constexpr double kPi = std::numbers::pi;
inline constexpr float kPiF = std::numbers::pi_v<float>;
inline constexpr double kDegToRad = std::numbers::pi / 180.0;
inline constexpr float kDegToRadF = std::numbers::pi_v<float> / 180.0f;
inline constexpr double kRadToDeg = 180.0 / std::numbers::pi;
inline constexpr float kRadToDegF = 180.0f / std::numbers::pi_v<float>;

struct Vec2 {
    float x = 0.0f, y = 0.0f;
    Vec2() = default;
    Vec2(float x, float y) : x(x), y(y) {}
};

// ---- 3成分ベクトル（Y-up 左手系。既存描画コードの流儀に合わせる） ----
struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;

    Vec3() = default;
    Vec3(float x, float y, float z) : x(x), y(y), z(z) {}

    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }

    float Length() const { return std::sqrt(x * x + y * y + z * z); }

    Vec3 Normalized() const {
        float l = Length();
        if (l > rs::core::kNormalizeLengthEps) return {x / l, y / l, z / l};
        return {0, 0, 0};
    }

    static float Dot(const Vec3& a, const Vec3& b) {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }
    static Vec3 Cross(const Vec3& a, const Vec3& b) {
        return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    }
};

// ---- 3成分ベクトル(double。計算用。GPUへ渡す境界でVec3へ変換する) ----
struct DVec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

inline DVec3 Sub(const DVec3& a, const DVec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline DVec3 Add(const DVec3& a, const DVec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline DVec3 Scale(const DVec3& a, double s) { return {a.x * s, a.y * s, a.z * s}; }
inline double Dot(const DVec3& a, const DVec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline DVec3 Cross(const DVec3& a, const DVec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline double Length(const DVec3& a) { return std::sqrt(Dot(a, a)); }

// 同じ射影を画面・水平面・空間で共有する。閾値の単位は入力座標の二乗。
inline double ClosestSegmentParameter(const DVec3& a, const DVec3& b, const DVec3& point,
                                      double lengthSquaredEps) {
    const DVec3 segment = Sub(b, a);
    const double lengthSquared = Dot(segment, segment);
    if (lengthSquared <= lengthSquaredEps) return 0.0;
    return std::clamp(Dot(Sub(point, a), segment) / lengthSquared, 0.0, 1.0);
}
// 長さが0以下のときは零ベクトルを返す。
inline DVec3 Normalize(const DVec3& a) {
    const double length = Length(a);
    return length > 0.0 ? Scale(a, 1.0 / length) : DVec3{};
}

// ---- 簡易 4x4 行列（row-major, row-vector 慣習: v' = v * M）----
struct Mat4 {
    float m[16];
    static Mat4 Identity() {
        return {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
    }
    static Mat4 Mul(const Mat4& a, const Mat4& b) {
        Mat4 r{};
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) {
                float s = 0;
                for (int k = 0; k < 4; ++k) s += a.m[i * 4 + k] * b.m[k * 4 + j];
                r.m[i * 4 + j] = s;
            }
        return r;
    }
};

// 剛体変換(直交回転+平行移動, row-vector 慣習: v' = v * M)の逆行列。
// 回転部が直交行列である前提で、一般逆行列でなく転置ベースの軽い式で求める。
inline Mat4 InverseRigid(const Mat4& m) {
    Mat4 r = Mat4::Identity();
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            r.m[i * 4 + j] = m.m[j * 4 + i];
    const float tx = m.m[12], ty = m.m[13], tz = m.m[14];
    r.m[12] = -(tx * r.m[0] + ty * r.m[4] + tz * r.m[8]);
    r.m[13] = -(tx * r.m[1] + ty * r.m[5] + tz * r.m[9]);
    r.m[14] = -(tx * r.m[2] + ty * r.m[6] + tz * r.m[10]);
    return r;
}

// row-vector 慣習(v' = v * M)の平行移動行列。平行移動成分は m[12..14] に格納する。
inline Mat4 MakeTranslation(float x, float y, float z) {
    Mat4 result = Mat4::Identity();
    result.m[12] = x;
    result.m[13] = y;
    result.m[14] = z;
    return result;
}

inline void Normalize3(float v[3]) {
    float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l > rs::core::kNormalizeLengthEps) {
        v[0] /= l;
        v[1] /= l;
        v[2] /= l;
    }
}
inline void Cross3(const float a[3], const float b[3], float o[3]) {
    o[0] = a[1] * b[2] - a[2] * b[1];
    o[1] = a[2] * b[0] - a[0] * b[2];
    o[2] = a[0] * b[1] - a[1] * b[0];
}

// LookAt (row-vector 慣習)
inline Mat4 LookAt(const float eye[3], const float at[3], const float up[3]) {
    float zaxis[3] = {at[0] - eye[0], at[1] - eye[1], at[2] - eye[2]};
    Normalize3(zaxis);
    float xaxis[3];
    Cross3(up, zaxis, xaxis);
    Normalize3(xaxis);
    float yaxis[3];
    Cross3(zaxis, xaxis, yaxis);
    auto dot = [](const float* p, const float* q) {
        return p[0] * q[0] + p[1] * q[1] + p[2] * q[2];
    };
    Mat4 r = Mat4::Identity();
    r.m[0] = xaxis[0]; r.m[1] = yaxis[0]; r.m[2] = zaxis[0]; r.m[3] = 0;
    r.m[4] = xaxis[1]; r.m[5] = yaxis[1]; r.m[6] = zaxis[1]; r.m[7] = 0;
    r.m[8] = xaxis[2]; r.m[9] = yaxis[2]; r.m[10] = zaxis[2]; r.m[11] = 0;
    r.m[12] = -dot(xaxis, eye);
    r.m[13] = -dot(yaxis, eye);
    r.m[14] = -dot(zaxis, eye);
    r.m[15] = 1;
    return r;
}

// 左手 Perspective (DX, row-vector)
// Reversed-Z: 深度をzn->1, zf->0にマッピングする（浮動小数点深度バッファの
// 精度を遠方まで均等に配分し、Zファイティングを抑えるための標準的な手法）。
inline Mat4 PerspectiveLH(float fovY, float aspect, float zn, float zf) {
    float yScale = 1.0f / std::tan(fovY * 0.5f);
    float xScale = yScale / aspect;
    Mat4 r{};
    r.m[0] = xScale;
    r.m[5] = yScale;
    r.m[10] = zn / (zn - zf);
    r.m[11] = 1.0f;
    r.m[14] = zn * zf / (zf - zn);
    return r;
}

// row-vector 慣習(v'=v*M)の4x4逆行列（ガウス・ジョルダン消去法）。
// 特異に近い場合は false を返す(呼び出し側は unproject をスキップする)。
inline bool InvertMat4(const Mat4& in, Mat4& out) {
    double a[4][8];
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) a[i][j] = double(in.m[i * 4 + j]);
        for (int j = 0; j < 4; ++j) a[i][4 + j] = (i == j) ? 1.0 : 0.0;
    }
    for (int col = 0; col < 4; ++col) {
        int pivot = col;
        double best = std::fabs(a[col][col]);
        for (int r2 = col + 1; r2 < 4; ++r2) {
            double v = std::fabs(a[r2][col]);
            if (v > best) { best = v; pivot = r2; }
        }
        if (best < kMatrixPivotEps) return false;
        if (pivot != col) {
            for (int j = 0; j < 8; ++j) std::swap(a[col][j], a[pivot][j]);
        }
        double d = a[col][col];
        for (int j = 0; j < 8; ++j) a[col][j] /= d;
        for (int r2 = 0; r2 < 4; ++r2) {
            if (r2 == col) continue;
            double f = a[r2][col];
            if (f == 0.0) continue;
            for (int j = 0; j < 8; ++j) a[r2][j] -= f * a[col][j];
        }
    }
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) out.m[i * 4 + j] = float(a[i][4 + j]);
    return true;
}

// row-vector 慣習: v' = v * M なので、点 p(1x4, w=1) の変換は p*M。
inline void TransformPoint4(const Mat4& m, const float p[4], float out[4]) {
    for (int j = 0; j < 4; ++j) {
        out[j] = p[0] * m.m[0 * 4 + j] + p[1] * m.m[1 * 4 + j] + p[2] * m.m[2 * 4 + j] +
                 p[3] * m.m[3 * 4 + j];
    }
}

// PartXform などHLSLレイアウト互換の生配列にも同じrow-vector変換を適用する。
inline void TransformPoint4(const float matrix[16], const float p[4], float out[4]) {
    for (int j = 0; j < 4; ++j) {
        out[j] = p[0] * matrix[0 * 4 + j] + p[1] * matrix[1 * 4 + j] +
                 p[2] * matrix[2 * 4 + j] + p[3] * matrix[3 * 4 + j];
    }
}

// ---- スクリーン投影・レイキャスト ----
// viewProj行列とスクリーン座標(px、左上原点)の相互変換。app層(SceneRenderer等)からも使う汎用カメラ幾何で、
// editor層がapp層(gpu/CameraMath.h)へ依存する形を避けるため、core に置いている。

// スクリーン投影結果。w<=0 は画面裏（背後）を表し、描画側は線分ごと
// スキップすること。
struct ProjectedPoint {
    float z = 0;
    float x = 0, y = 0; // スクリーン座標(px、左上原点)
    float w = 0;        // クリップw。w<=0 なら画面外(背後)扱い
    bool Visible() const { return w > 0.0f; }
};

// レイ(origin, dir)。dir は正規化済み。
struct Ray {
    Vec3 origin;
    Vec3 dir;
};

// レイ(t >= 0) と3D線分(u=[0,1])の最短距離を返す。outRayT/outSegmentTは各々の最近傍点の
// パラメータであり、退化した線分も点として扱う。
inline bool RayClosestPointOnSegment(const Ray& ray, const Vec3& segmentA,
                                     const Vec3& segmentB, float& outRayT,
                                     float& outSegmentT, float& outDistance) {
    const Vec3 segment = segmentB - segmentA;
    const Vec3 offset = ray.origin - segmentA;
    const float segmentLengthSq = Vec3::Dot(segment, segment);
    if (segmentLengthSq <= kHorizontalSegmentLengthSquaredEpsM2) {
        outSegmentT = 0.0f;
        outRayT = std::max(0.0f, Vec3::Dot(segmentA - ray.origin, ray.dir));
        outDistance = (ray.origin + ray.dir * outRayT - segmentA).Length();
        return true;
    }

    const float directionLengthSq = Vec3::Dot(ray.dir, ray.dir);
    if (directionLengthSq <= kRayDirectionLengthSquaredEps) return false;
    const float directionSegment = Vec3::Dot(ray.dir, segment);
    const float directionOffset = Vec3::Dot(ray.dir, offset);
    const float segmentOffset = Vec3::Dot(segment, offset);
    const float denominator = directionLengthSq * segmentLengthSq -
                              directionSegment * directionSegment;
    if (denominator > rs::core::kRayParallelEps) {
        outRayT = std::max(0.0f, (directionSegment * segmentOffset -
                                  segmentLengthSq * directionOffset) / denominator);
    } else {
        outRayT = 0.0f;
    }
    outSegmentT = std::clamp((directionSegment * outRayT + segmentOffset) / segmentLengthSq,
                             0.0f, 1.0f);
    outRayT = std::max(0.0f, (directionSegment * outSegmentT - directionOffset) /
                                  directionLengthSq);
    const Vec3 rayPoint = ray.origin + ray.dir * outRayT;
    const Vec3 segmentPoint = segmentA + segment * outSegmentT;
    outDistance = (rayPoint - segmentPoint).Length();
    return true;
}

// worldPos -> スクリーン座標(viewProj使用)。
inline ProjectedPoint ProjectToScreen(const Vec3& worldPos, const Mat4& viewProj, uint32_t width,
                                      uint32_t height) {
    float p[4] = {worldPos.x, worldPos.y, worldPos.z, 1.0f};
    float clip[4];
    TransformPoint4(viewProj, p, clip);
    ProjectedPoint out;
    out.w = clip[3];
    if (clip[3] <= 0.0f) return out; // 画面裏
    float ndcX = clip[0] / clip[3];
    float ndcY = clip[1] / clip[3];
    out.x = (ndcX * 0.5f + 0.5f) * float(width);
    out.y = (1.0f - (ndcY * 0.5f + 0.5f)) * float(height); // NDC Y+ は上->スクリーンY+は下
    out.z = clip[2] / clip[3];
    return out;
}

// スクリーン(x,y) -> ワールドレイ(逆投影)。viewProjの逆行列で
// near/far点をunprojectする(reversed-Z: クリップz near=1, far=0)。
// 逆行列が求まらない場合は false を返す。
inline bool ScreenToRay(float screenX, float screenY, const Mat4& viewProj, uint32_t width,
                        uint32_t height, Ray& outRay) {
    Mat4 inv;
    if (!InvertMat4(viewProj, inv)) return false;

    float ndcX = (width > 0) ? (screenX / float(width)) * 2.0f - 1.0f : 0.0f;
    float ndcY = (height > 0) ? 1.0f - (screenY / float(height)) * 2.0f : 0.0f;

    // Reversed-Z: near=クリップz 1.0, far=クリップz 0.0。
    auto unproject = [&](float ndcZ, Vec3& outPos) -> bool {
        float clip[4] = {ndcX, ndcY, ndcZ, 1.0f};
        float world[4];
        TransformPoint4(inv, clip, world);
        if (std::fabs(world[3]) < rs::core::kHomogeneousWEps) return false;
        outPos = Vec3(world[0] / world[3], world[1] / world[3], world[2] / world[3]);
        return true;
    };

    Vec3 nearPos, farPos;
    if (!unproject(1.0f, nearPos)) return false;
    if (!unproject(0.0f, farPos)) return false;

    Vec3 dir = (farPos - nearPos);
    float len = dir.Length();
    if (len < rs::core::kNormalizeLengthEps) return false;
    outRay.origin = nearPos;
    outRay.dir = dir * (1.0f / len);
    return true;
}

inline float WorldRadiusForPixels(const Mat4& viewProj, uint32_t width, uint32_t height,
                                  float screenX, float screenY, const Vec3& point,
                                  float pxRadius) {
    if (pxRadius <= 0.0f) return 0.0f;
    Ray ray0;
    Ray ray1;
    if (!ScreenToRay(screenX, screenY, viewProj, width, height, ray0) ||
        !ScreenToRay(screenX + pxRadius, screenY, viewProj, width, height, ray1)) {
        return 0.0f;
    }
    return (ray1.dir - ray0.dir).Length() * (point - ray0.origin).Length();
}

// 2つのベクトルのなす角[度]。atan2(外積の長さ, 内積) で求める。acos(内積) だと、ほぼ同じ向きのとき
// float の内積が1より1ビット小さくなるだけで約0.02度の誤差に見えるため。ゼロベクトルは0度。
inline double AngleBetweenDeg(const Vec3& a, const Vec3& b) {
    return std::atan2(static_cast<double>(Vec3::Cross(a, b).Length()),
                      static_cast<double>(Vec3::Dot(a, b))) * 180.0 / std::numbers::pi;
}

// レイ vs Y=planeY 水平面のヒット点。平行(dir.y≈0)や後方(t<0)なら false。
inline bool RayHitPlaneY(const Ray& ray, float planeY, Vec3& outHit) {
    if (std::fabs(ray.dir.y) < rs::core::kRayParallelEps) return false;
    float t = (planeY - ray.origin.y) / ray.dir.y;
    if (t < 0.0f) return false;
    outHit = ray.origin + ray.dir * t;
    return true;
}

// レイ vs Y=0平面のヒット点。平行(dir.y≈0)なら false。
inline bool RayHitGroundPlane(const Ray& ray, Vec3& outHit) {
    return RayHitPlaneY(ray, 0.0f, outHit);
}

} // namespace rs::core
